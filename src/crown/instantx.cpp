// Copyright (c) 2009-2015 The Dash developers
// Copyright (c) 2014-2018 The Crown developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <base58.h>
#include <consensus/validation.h>
#include <crown/instantx.h>
#include <crown/legacycalls.h>
#include <crown/legacysigner.h>
#include <crown/spork.h>
#include <key.h>
#include <masternode/activemasternode.h>
#include <masternode/masternodeman.h>
#include <net.h>
#include <net_processing.h>
#include <node/context.h>
#include <rpc/blockchain.h>
#include <protocol.h>
#include <sync.h>
#include <util/system.h>
#include <wallet/wallet.h>

#include <boost/lexical_cast.hpp>

using namespace std;
using namespace boost;

InstantSend instantSend;

//step 1.) Broadcast intention to lock transaction inputs, "txlreg", CTransaction
//step 2.) Top INSTANTX_SIGNATURES_TOTAL masternodes, open connect to top 1 masternode.
//         Send "txvote", CTransaction, Signature, Approve
//step 3.) Top 1 masternode, waits for INSTANTX_SIGNATURES_REQUIRED messages. Upon success, sends "txlock'

void InstantSend::ProcessMessage(CNode* pfrom, const std::string& strCommand, CDataStream& vRecv, CConnman* connman)
{
    if (fLiteMode)
        return; //disable all masternode related functionality
    if (!IsSporkActive(SPORK_2_INSTANTX))
        return;
    if (!masternodeSync.IsBlockchainSynced())
        return;

    if (strCommand == "ix") {

        LogPrintf("ProcessMessageInstantX::ix\n");

        CDataStream vMsg(vRecv);
        CMutableTransaction tx;
        vRecv >> tx;

        CInv inv(MSG_TXLOCK_REQUEST, tx.GetHash());
        pfrom->AddInventoryKnown(inv);

        if (m_txLockReq.count(tx.GetHash()) || m_txLockReqRejected.count(tx.GetHash()))
            return;

        if (!IsIxTxValid(tx))
            return;

        // Check if transaction is old for lock
        if (GetTransactionAge(tx.GetHash()) > m_acceptedBlockCount)
            return;

        int64_t nBlockHeight = CreateNewLock(tx);
        if (nBlockHeight == 0)
            return;

        TxValidationState state;
        bool fAccepted = false;
        {
            LOCK(cs_main);
            fAccepted = AcceptToMemoryPool(*g_rpc_node->mempool, state, MakeTransactionRef(tx), nullptr, true);
        }

        if (fAccepted) {
            connman->RelayInv(inv);

            DoConsensusVote(tx, nBlockHeight, *connman);

            m_txLockReq.insert(make_pair(tx.GetHash(), tx));

            LogPrintf("ProcessMessageInstantX::ix - Transaction Lock Request: %s %s : accepted %s\n",
                pfrom->addr.ToString().c_str(), pfrom->cleanSubVer.c_str(),
                tx.GetHash().ToString().c_str());

            return;

        } else {
            m_txLockReqRejected.insert(make_pair(tx.GetHash(), tx));

            // can we get the conflicting transaction as proof?

            LogPrintf("ProcessMessageInstantX::ix - Transaction Lock Request: %s %s : rejected %s\n",
                pfrom->addr.ToString().c_str(), pfrom->cleanSubVer.c_str(),
                tx.GetHash().ToString().c_str());

            for (const auto& in : tx.vin) {
                if (!m_lockedInputs.count(in.prevout)) {
                    m_lockedInputs.insert(make_pair(in.prevout, tx.GetHash()));
                }
            }

            // resolve conflicts
            std::map<uint256, CTransactionLock>::iterator i = m_txLocks.find(tx.GetHash());
            if (i != m_txLocks.end()) {
                //we only care if we have a complete tx lock
                if ((*i).second.CountSignatures() >= INSTANTX_SIGNATURES_REQUIRED) {
                    if (!CheckForConflictingLocks(tx)) {
                        LogPrintf("ProcessMessageInstantX::ix - Found Existing Complete IX Lock\n");

                        //reprocess the last 15 blocks
                        ReprocessBlocks(15);
                        m_txLockReq.insert(make_pair(tx.GetHash(), tx));
                    }
                }
            }

            return;
        }
    } else if (strCommand == "txlvote") //InstantX Lock Consensus Votes
    {
        CConsensusVote ctx;
        vRecv >> ctx;

        CInv inv(MSG_TXLOCK_VOTE, ctx.GetHash());
        pfrom->AddInventoryKnown(inv);

        if (m_txLockVote.find(ctx.GetHash()) != m_txLockVote.end())
            return;

        // Check if transaction is old for lock
        if (GetTransactionAge(ctx.txHash) > m_acceptedBlockCount) {
            LogPrintf("InstantSend::ProcessMessage - Old transaction lock request is received. TxId - %s\n", ctx.txHash.ToString());
            return;
        }

        m_txLockVote.insert(make_pair(ctx.GetHash(), ctx));

        if (ProcessConsensusVote(pfrom, ctx, *connman)) {
            /*
                Masternodes will sometimes propagate votes before the transaction is known to the client.
                This tracks those messages and allows it at the same rate of the rest of the network, if
                a peer violates it, it will simply be ignored
            */
            if (!m_txLockReq.count(ctx.txHash) && !m_txLockReqRejected.count(ctx.txHash)) {
                if (!m_unknownVotes.count(ctx.vinMasternode.prevout.hash)) {
                    m_unknownVotes[ctx.vinMasternode.prevout.hash] = GetTime() + (60 * 10);
                }

                if (m_unknownVotes[ctx.vinMasternode.prevout.hash] > GetTime() && m_unknownVotes[ctx.vinMasternode.prevout.hash] - GetAverageVoteTime() > 60 * 10) {
                    LogPrintf("ProcessMessageInstantX::ix - masternode is spamming transaction votes: %s %s\n",
                        ctx.vinMasternode.ToString().c_str(),
                        ctx.txHash.ToString().c_str());
                    return;
                } else {
                    m_unknownVotes[ctx.vinMasternode.prevout.hash] = GetTime() + (60 * 10);
                }
            }
            connman->RelayInv(inv);
        }
        return;
    }

    else if (strCommand == "txllist") //Get InstantX Locked list
    {
        std::map<uint256, CConsensusVote>::const_iterator it = m_txLockVote.begin();
        for (; it != m_txLockVote.end(); ++it) {
            CInv inv(MSG_TXLOCK_VOTE, it->second.GetHash());
            pfrom->AddInventoryKnown(inv);

            connman->RelayInv(inv);
        }
    }
}

bool InstantSend::IsIxTxValid(const CMutableTransaction& txCollateral) const
{
    if (txCollateral.vout.size() < 1)
        return false;
    if (txCollateral.nLockTime != 0)
        return false;

    int64_t nValueIn = 0;
    int64_t nValueOut = 0;
    bool missingTx = false;

    for (const auto& o : txCollateral.vout)
        nValueOut += o.nValue;

    for (const auto& i : txCollateral.vin) {
        uint256 hash;
        CTransactionRef tx2 = GetTransaction(::ChainActive().Tip(), nullptr, i.prevout.hash, Params().GetConsensus(), hash);
        if (tx2->vout.size() > i.prevout.n)
            nValueIn += tx2->vout[i.prevout.n].nValue;
        else
            missingTx = true;
    }

    if (nValueOut > GetSporkValue(SPORK_5_MAX_VALUE) * COIN) {
        LogPrintf("IsIxTxValid - Transaction value too high - %s\n", txCollateral.GetHash().ToString().c_str());
        return false;
    }

    if (missingTx) {
        LogPrintf("IsIxTxValid - Unknown inputs in IX transaction - %s\n", txCollateral.GetHash().ToString().c_str());
        /*
            This happens sometimes for an unknown reason, so we'll return that it's a valid transaction.
            If someone submits an invalid transaction it will be rejected by the network anyway and this isn't
            very common, but we don't want to block IX just because the client can't figure out the fee.
        */
        return true;
    }

    return true;
}

int64_t InstantSend::CreateNewLock(const CMutableTransaction& tx)
{
    LOCK(cs);
    int64_t nTxAge = 0;
    for (const auto& i : tx.vin) {
        nTxAge = GetUTXOConfirmations(i.prevout);
        if (nTxAge < 5) //1 less than the "send IX" gui requires, incase of a block propagating the network at the time
        {
            LogPrintf("CreateNewLock - Transaction not found / too new: %d / %s\n", nTxAge, tx.GetHash().ToString().c_str());
            return 0;
        }
    }

    /*
        Use a blockheight newer than the input.
        This prevents attackers from using transaction mallibility to predict which masternodes
        they'll use.
    */
    int nBlockHeight = (::ChainActive().Tip()->nHeight - nTxAge) + 4;

    if (!m_txLocks.count(tx.GetHash())) {
        LogPrintf("CreateNewLock - New Transaction Lock %s !\n", tx.GetHash().ToString().c_str());

        CTransactionLock newLock;
        newLock.nBlockHeight = nBlockHeight;
        newLock.txHash = tx.GetHash();
        m_txLocks.insert(make_pair(tx.GetHash(), newLock));
    } else {
        m_txLocks[tx.GetHash()].nBlockHeight = nBlockHeight;
        LogPrintf("CreateNewLock - Transaction Lock Exists %s !\n", tx.GetHash().ToString().c_str());
    }

    m_txLockReq.insert(make_pair(tx.GetHash(), tx));
    return nBlockHeight;
}

// check if we need to vote on this transaction
void InstantSend::DoConsensusVote(const CMutableTransaction& tx, int64_t nBlockHeight, CConnman& connman)
{
    LOCK(cs);
    if (!fMasterNode)
        return;

    int n = mnodeman.GetMasternodeRank(activeMasternode.vin, nBlockHeight, MIN_INSTANTX_PROTO_VERSION);

    if (n == -1) {
        LogPrintf("InstantX::DoConsensusVote - Unknown Masternode\n");
        return;
    }

    if (n > INSTANTX_SIGNATURES_TOTAL) {
        LogPrintf("InstantX::DoConsensusVote - Masternode not in the top %d (%d)\n", INSTANTX_SIGNATURES_TOTAL, n);
        return;
    }
    /*
        nBlockHeight calculated from the transaction is the authoritive source
    */

    LogPrintf("InstantX::DoConsensusVote - In the top %d (%d)\n", INSTANTX_SIGNATURES_TOTAL, n);

    CConsensusVote ctx;
    ctx.vinMasternode = activeMasternode.vin;
    ctx.txHash = tx.GetHash();
    ctx.nBlockHeight = nBlockHeight;
    if (!ctx.Sign()) {
        LogPrintf("InstantX::DoConsensusVote - Failed to sign consensus vote\n");
        return;
    }
    if (!ctx.SignatureValid()) {
        LogPrintf("InstantX::DoConsensusVote - Signature invalid\n");
        return;
    }

    m_txLockVote[ctx.GetHash()] = ctx;

    CInv inv(MSG_TXLOCK_VOTE, ctx.GetHash());
    connman.RelayInv(inv);
}

//received a consensus vote
bool InstantSend::ProcessConsensusVote(CNode* pnode, const CConsensusVote& ctx, CConnman& connman)
{
    LOCK(cs);
    int n = mnodeman.GetMasternodeRank(ctx.vinMasternode, ctx.nBlockHeight, MIN_INSTANTX_PROTO_VERSION);

    CMasternode* pmn = mnodeman.Find(ctx.vinMasternode);
    if (pmn != NULL)
        LogPrintf("InstantX::ProcessConsensusVote - Masternode ADDR %s %d\n", pmn->addr.ToString().c_str(), n);

    if (n == -1) {
        //can be caused by past versions trying to vote with an invalid protocol
        LogPrintf("InstantX::ProcessConsensusVote - Unknown Masternode\n");
        mnodeman.AskForMN(pnode, ctx.vinMasternode, connman);
        return false;
    }

    if (n > INSTANTX_SIGNATURES_TOTAL) {
        LogPrintf("InstantX::ProcessConsensusVote - Masternode not in the top %d (%d) - %s\n", INSTANTX_SIGNATURES_TOTAL, n, ctx.GetHash().ToString().c_str());
        return false;
    }

    if (!ctx.SignatureValid()) {
        LogPrintf("InstantX::ProcessConsensusVote - Signature invalid\n");
        // don't ban, it could just be a non-synced masternode
        mnodeman.AskForMN(pnode, ctx.vinMasternode, connman);
        return false;
    }

    if (!m_txLocks.count(ctx.txHash)) {
        LogPrintf("InstantX::ProcessConsensusVote - New Transaction Lock %s !\n", ctx.txHash.ToString().c_str());

        CTransactionLock newLock;
        newLock.nBlockHeight = 0;
        newLock.txHash = ctx.txHash;
        m_txLocks.insert(make_pair(ctx.txHash, newLock));
    } else
        LogPrintf("InstantX::ProcessConsensusVote - Transaction Lock Exists %s !\n", ctx.txHash.ToString().c_str());

    //compile consessus vote
    std::map<uint256, CTransactionLock>::iterator i = m_txLocks.find(ctx.txHash);
    if (i != m_txLocks.end()) {
        (*i).second.AddSignature(ctx);

        //! note: mapRequests code removed, as the client doesnt test propogation success this way anymore.

        LogPrintf("InstantX::ProcessConsensusVote - Transaction Lock Votes %d - %s !\n", (*i).second.CountSignatures(), ctx.GetHash().ToString().c_str());

        if ((*i).second.CountSignatures() >= INSTANTX_SIGNATURES_REQUIRED) {
            LogPrintf("InstantX::ProcessConsensusVote - Transaction Lock Is Complete \n");
            LogPrintf("InstantX::ProcessConsensusVote - Transaction Lock Is Complete %s !\n", (*i).second.GetHash().ToString().c_str());

            CMutableTransaction& tx = m_txLockReq[ctx.txHash];
            if (!CheckForConflictingLocks(tx)) {

                if (m_txLockReq.count(ctx.txHash)) {
                    for (const auto& in : tx.vin) {
                        if (!m_lockedInputs.count(in.prevout)) {
                            m_lockedInputs.insert(make_pair(in.prevout, ctx.txHash));
                        }
                    }
                }

                // resolve conflicts

                //if this tx lock was rejected, we need to remove the conflicting blocks
                if (m_txLockReqRejected.count((*i).second.txHash)) {
                    //reprocess the last 15 blocks
                    ReprocessBlocks(15);
                }
            }
        }
        return true;
    }

    return false;
}

bool InstantSend::CheckForConflictingLocks(const CMutableTransaction& tx)
{
    LOCK(cs);
    /*
        It's possible (very unlikely though) to get 2 conflicting transaction locks approved by the network.
        In that case, they will cancel each other out.

        Blocks could have been rejected during this time, which is OK. After they cancel out, the client will
        rescan the blocks and find they're acceptable and then take the chain with the most work.
    */
    for (const auto& in : tx.vin) {
        if (m_lockedInputs.count(in.prevout)) {
            if (m_lockedInputs[in.prevout] != tx.GetHash()) {
                LogPrintf("InstantX::CheckForConflictingLocks - found two complete conflicting locks - removing both. %s %s",
                    tx.GetHash().ToString().c_str(), m_lockedInputs[in.prevout].ToString().c_str());
                if (m_txLocks.count(tx.GetHash()))
                    m_txLocks[tx.GetHash()].m_expiration = GetTime();
                if (m_txLocks.count(m_lockedInputs[in.prevout]))
                    m_txLocks[m_lockedInputs[in.prevout]].m_expiration = GetTime();
                return true;
            }
        }
    }
    return false;
}

int64_t InstantSend::GetAverageVoteTime() const
{
    std::map<uint256, int64_t>::const_iterator it = m_unknownVotes.begin();
    int64_t total = 0;
    int64_t count = 0;

    while (it != m_unknownVotes.end()) {
        total += it->second;
        count++;
        it++;
    }

    return total / count;
}

void InstantSend::CheckAndRemove()
{
    LOCK(cs);
    if (::ChainActive().Tip() == NULL)
        return;

    std::map<uint256, CTransactionLock>::iterator it = m_txLocks.begin();

    while (it != m_txLocks.end()) {
        if (GetTime() > it->second.m_expiration) {
            LogPrintf("Removing old transaction lock %s\n", it->second.txHash.ToString().c_str());

            // Remove rejected transaction if expired
            m_txLockReqRejected.erase(it->second.txHash);

            std::map<uint256, CMutableTransaction>::iterator itLock = m_txLockReq.find(it->second.txHash);
            if (itLock != m_txLockReq.end()) {
                CMutableTransaction& tx = itLock->second;

                for (const auto& in : tx.vin)
                    m_lockedInputs.erase(in.prevout);

                m_txLockReq.erase(it->second.txHash);

                for (const auto& v : it->second.vecConsensusVotes)
                    m_txLockVote.erase(v.GetHash());
            }
            m_txLocks.erase(it++);
        } else {
            it++;
        }
    }

    std::map<uint256, CConsensusVote>::iterator itVote = m_txLockVote.begin();
    while (itVote != m_txLockVote.end()) {
        if (GetTime() > it->second.m_expiration || GetTransactionAge(it->second.txHash) > InstantSend::m_completeTxLocks) {
            // Remove transaction vote if it is expired or belongs to old transaction
            m_txLockVote.erase(itVote++);
        } else {
            ++itVote;
        }
    }
}

int InstantSend::GetSignaturesCount(uint256 txHash) const
{
    std::map<uint256, CTransactionLock>::const_iterator i = m_txLocks.find(txHash);
    if (i != m_txLocks.end()) {
        return (*i).second.CountSignatures();
    }
    return -1;
}

bool InstantSend::IsLockTimedOut(uint256 txHash) const
{
    std::map<uint256, CTransactionLock>::const_iterator i = m_txLocks.find(txHash);
    if (i != m_txLocks.end()) {
        return GetTime() > (*i).second.m_timeout;
    }
    return false;
}

bool InstantSend::TxLockRequested(uint256 txHash) const
{
    return m_txLockReq.count(txHash) || m_txLockReqRejected.count(txHash);
}

boost::optional<uint256> InstantSend::GetLockedTx(const COutPoint& out) const
{
    std::map<COutPoint, uint256>::const_iterator it = m_lockedInputs.find(out);
    if (it != m_lockedInputs.end())
        return boost::optional<uint256>(it->second);
    return boost::optional<uint256>();
}

boost::optional<CConsensusVote> InstantSend::GetLockVote(uint256 txHash) const
{
    std::map<uint256, CConsensusVote>::const_iterator it = m_txLockVote.find(txHash);
    if (it != m_txLockVote.end())
        return boost::optional<CConsensusVote>(it->second);
    return boost::optional<CConsensusVote>();
}

boost::optional<CMutableTransaction> InstantSend::GetLockReq(uint256 txHash) const
{
    std::map<uint256, CMutableTransaction>::const_iterator it = m_txLockReq.find(txHash);
    if (it != m_txLockReq.end())
        return boost::optional<CMutableTransaction>(it->second);
    return boost::optional<CMutableTransaction>();
}

bool InstantSend::AlreadyHave(uint256 txHash) const
{
    return m_txLockVote.find(txHash) != m_txLockVote.end();
}

std::string InstantSend::ToString() const
{
    std::ostringstream info;

    info << "Transaction lock requests: " << m_txLockReq.size() << ", Transaction locks: " << m_txLocks.size() << ", Locked Inputs: " << m_lockedInputs.size() << ", Transaction lock votes: " << m_txLockVote.size();

    return info.str();
}

void InstantSend::Clear()
{
    LOCK(cs);
    m_lockedInputs.clear();
    m_txLockVote.clear();
    m_txLockReq.clear();
    m_txLocks.clear();
    m_unknownVotes.clear();
    m_txLockReqRejected.clear();
}

int InstantSend::GetCompleteLocksCount() const
{
    return m_completeTxLocks;
}

uint256 CConsensusVote::GetHash() const
{
    return ArithToUint256(UintToArith256(vinMasternode.prevout.hash) + vinMasternode.prevout.n + UintToArith256(txHash));
}

bool CConsensusVote::SignatureValid() const
{
    std::string errorMessage;
    std::string strMessage = txHash.ToString().c_str() + boost::lexical_cast<std::string>(nBlockHeight);
    //LogPrintf("verify strMessage %s \n", strMessage.c_str());

    CMasternode* pmn = mnodeman.Find(vinMasternode);

    if (pmn == NULL) {
        LogPrintf("InstantX::CConsensusVote::SignatureValid() - Unknown Masternode\n");
        return false;
    }

    if (!legacySigner.VerifyMessage(pmn->pubkey2, vchMasterNodeSignature, strMessage, errorMessage)) {
        LogPrintf("InstantX::CConsensusVote::SignatureValid() - Verify message failed\n");
        return false;
    }

    return true;
}

bool CConsensusVote::Sign()
{
    std::string errorMessage;

    CKey key2;
    CPubKey pubkey2;
    std::string strMessage = txHash.ToString().c_str() + boost::lexical_cast<std::string>(nBlockHeight);
    //LogPrintf("signing strMessage %s \n", strMessage.c_str());
    //LogPrintf("signing privkey %s \n", strMasterNodePrivKey.c_str());

    if (!legacySigner.SetKey(strMasterNodePrivKey, key2, pubkey2)) {
        LogPrintf("CConsensusVote::Sign() - ERROR: Invalid masternodeprivkey: '%s'\n", errorMessage.c_str());
        return false;
    }

    if (!legacySigner.SignMessage(strMessage, errorMessage, vchMasterNodeSignature, key2)) {
        LogPrintf("CConsensusVote::Sign() - Sign message failed");
        return false;
    }

    if (!legacySigner.VerifyMessage(pubkey2, vchMasterNodeSignature, strMessage, errorMessage)) {
        LogPrintf("CConsensusVote::Sign() - Verify message failed");
        return false;
    }

    return true;
}

bool CTransactionLock::SignaturesValid() const
{

    for (const auto& vote : vecConsensusVotes) {
        int n = mnodeman.GetMasternodeRank(vote.vinMasternode, vote.nBlockHeight, MIN_INSTANTX_PROTO_VERSION);

        if (n == -1) {
            LogPrintf("CTransactionLock::SignaturesValid() - Unknown Masternode\n");
            return false;
        }

        if (n > INSTANTX_SIGNATURES_TOTAL) {
            LogPrintf("CTransactionLock::SignaturesValid() - Masternode not in the top %s\n", INSTANTX_SIGNATURES_TOTAL);
            return false;
        }

        if (!vote.SignatureValid()) {
            LogPrintf("CTransactionLock::SignaturesValid() - Signature not valid\n");
            return false;
        }
    }

    return true;
}

void CTransactionLock::AddSignature(const CConsensusVote& cv)
{
    vecConsensusVotes.push_back(cv);
}

int CTransactionLock::CountSignatures() const
{
    /*
        Only count signatures where the BlockHeight matches the transaction's blockheight.
        The votes have no proof it's the correct blockheight
    */

    if (nBlockHeight == 0)
        return -1;

    int n = 0;
    for (const auto& v : vecConsensusVotes) {
        if (v.nBlockHeight == nBlockHeight) {
            n++;
        }
    }
    return n;
}

uint256 CTransactionLock::GetHash() const
{
    return txHash;
}
