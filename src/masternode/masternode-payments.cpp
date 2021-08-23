// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2014-2018 The Crown developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <addrman.h>
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>
#include <crown/legacysigner.h>
#include <crown/spork.h>
#include <key_io.h>
#include <masternode/masternode-budget.h>
#include <masternode/masternode-payments.h>
#include <masternode/masternode-sync.h>
#include <masternode/masternodeman.h>
#include <net_processing.h>
#include <netfulfilledman.h>
#include <netmessagemaker.h>
#include <sync.h>
#include <util/moneystr.h>
#include <util/system.h>

/** Object for who's going to get paid on which blocks */
CMasternodePayments masternodePayments;

RecursiveMutex cs_vecPayments;
RecursiveMutex cs_mapMasternodeBlocks;
RecursiveMutex cs_mapMasternodePayeeVotes;

bool IsBlockValueValid(const CBlock& block, int64_t nExpectedValue)
{
    CBlockIndex* pindexPrev = ::ChainActive().Tip();
    if (pindexPrev == nullptr)
        return true;

    bool fProofOfStake = block.IsProofOfStake();

    int nHeight = 0;
    if (pindexPrev->GetBlockHash() == block.hashPrevBlock) {
        nHeight = pindexPrev->nHeight + 1;
    } else { //out of order
        BlockMap::iterator mi = g_chainman.BlockIndex().find(block.hashPrevBlock);
        if (mi != g_chainman.BlockIndex().end() && (*mi).second)
            nHeight = (*mi).second->nHeight + 1;
    }

    if (nHeight == 0) {
        LogPrint(BCLog::NET, "IsBlockValueValid() : WARNING: Couldn't find previous block");
    }

    CAmount nBlockCreation = block.vtx[0]->GetValueOut();
    if (fProofOfStake)
        nBlockCreation += block.vtx[1]->GetValueOut();

    if (!masternodeSync.IsSynced()) { //there is no budget data to use to check anything
        //super blocks will always be on these blocks, max 100 per budgeting
        if (nHeight % GetBudgetPaymentCycleBlocks() < 100) {
            return true;
        } else {
            if (nBlockCreation > nExpectedValue)
                return false;
        }
    } else { // we're synced and have data so check the budget schedule

        //are these blocks even enabled
        if (!IsSporkActive(SPORK_13_ENABLE_SUPERBLOCKS)) {
            return nBlockCreation <= nExpectedValue;
        }

        if (budget.IsBudgetPaymentBlock(nHeight)) {
            //the value of the block is evaluated in CheckBlock
            return true;
        } else {
            if (nBlockCreation > nExpectedValue)
                return false;
        }
    }

    return true;
}

bool IsBlockPayeeValid(const CAmount& nAmountCreated, const CTransaction& txNew, int nBlockHeight, const uint32_t& nTime, const uint32_t& nTimePrevBlock)
{
    if (!masternodeSync.IsSynced()) { //there is no budget data to use to check anything -- find the longest chain
        if (gArgs.GetBoolArg("-debug", false))
            LogPrint(BCLog::NET, "Client not synced, skipping block payee checks\n");
        return true;
    }

    //check if it's a budget block
    if (IsSporkActive(SPORK_13_ENABLE_SUPERBLOCKS)) {
        if (budget.IsBudgetPaymentBlock(nBlockHeight)) {
            if (budget.IsTransactionValid(txNew, nBlockHeight)) {
                return true;
            } else {
                LogPrint(BCLog::NET, "Invalid budget payment detected %s\n", txNew.ToString().c_str());
                if (IsSporkActive(SPORK_9_MASTERNODE_BUDGET_ENFORCEMENT)) {
                    return false;
                } else {
                    LogPrint(BCLog::NET, "Budget enforcement is disabled, accepting block\n");
                    return true;
                }
            }
        }
    }

    //check for masternode payee
    if (masternodePayments.IsTransactionValid(nAmountCreated, txNew, nBlockHeight)) {
        return true;
    } else if (nTime - nTimePrevBlock > Params().GetConsensus().ChainStallDuration()) {
        // The chain has stalled, allow the first block to have no payment to winners
        LogPrint(BCLog::NET, "%s: Chain stall, time between blocks=%d\n", __func__, nTime - nTimePrevBlock);
        return true;
    } else {
        LogPrint(BCLog::NET, "Invalid mn payment detected %s\n", txNew.ToString().c_str());
        if (IsSporkActive(SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT)) {
            return false;
        } else {
            LogPrint(BCLog::NET, "Masternode payment enforcement is disabled, accepting block\n");
            return true;
        }
    }

    return false;
}

bool CMasternodePayments::CanVote(COutPoint outMasternode, int nBlockHeight)
{
    LOCK(cs_mapMasternodePayeeVotes);

    if (mapMasternodesLastVote.count(outMasternode) && mapMasternodesLastVote[outMasternode] == nBlockHeight) {
        return false;
    }

    //record this masternode voted
    mapMasternodesLastVote[outMasternode] = nBlockHeight;
    return true;
}

void FillBlockPayee(CMutableTransaction& txNew, int64_t nFees)
{
    CBlockIndex* pindexPrev = ::ChainActive().Tip();
    if (!pindexPrev)
        return;

    if (IsSporkActive(SPORK_13_ENABLE_SUPERBLOCKS) && budget.IsBudgetPaymentBlock(pindexPrev->nHeight + 1)) {
        budget.FillBlockPayee(txNew, nFees);
    } else {
        masternodePayments.FillBlockPayee(txNew, nFees);
    }
}

std::string GetRequiredPaymentsString(int nBlockHeight)
{
    if (IsSporkActive(SPORK_13_ENABLE_SUPERBLOCKS) && budget.IsBudgetPaymentBlock(nBlockHeight)) {
        return budget.GetRequiredPaymentsString(nBlockHeight);
    } else {
        return masternodePayments.GetRequiredPaymentsString(nBlockHeight);
    }
}

void CMasternodePayments::FillBlockPayee(CMutableTransaction& txNew, int64_t nFees)
{
    const Consensus::Params& consensusParams = Params().GetConsensus();

    CBlockIndex* pindexPrev = ::ChainActive().Tip();
    if (!pindexPrev)
        return;

    bool hasPayment = true;
    CScript payee;

    //spork
    if (!masternodePayments.GetBlockPayee(pindexPrev->nHeight + 1, payee)) {
        //no masternode detected
        CMasternode* winningNode = mnodeman.GetCurrentMasterNode(1);
        if (winningNode) {
            payee = GetScriptForDestination(PKHash(winningNode->pubkey));
        } else {
            LogPrint(BCLog::NET, "CreateNewBlock: Failed to detect masternode to pay\n");
            hasPayment = false;
        }
    }

    CAmount blockValue = GetBlockValue(pindexPrev->nHeight, nFees, consensusParams);
    CAmount masternodePayment = GetMasternodePayment(pindexPrev->nHeight + 1, blockValue, consensusParams);
    txNew.vout[0].nValue = blockValue;

    if (hasPayment) {
        txNew.vout.resize(2);

        txNew.vout[MN_PMT_SLOT].scriptPubKey = payee;
        txNew.vout[MN_PMT_SLOT].nValue = masternodePayment;

        txNew.vout[0].nValue -= masternodePayment;
        LogPrint(BCLog::NET, "Masternode payment to %s\n", EncodeDestination(ScriptHash(payee)));
    }
}

int CMasternodePayments::GetMinMasternodePaymentsProto()
{
    return IsSporkActive(SPORK_10_MASTERNODE_DONT_PAY_OLD_NODES)
        ? MIN_MASTERNODE_PAYMENT_PROTO_VERSION_CURR
        : MIN_MASTERNODE_PAYMENT_PROTO_VERSION_PREV;
}

void CMasternodePayments::ProcessMessageMasternodePayments(CNode* pfrom, const std::string& strCommand, CDataStream& vRecv, CConnman* connman)
{
    if (!masternodeSync.IsBlockchainSynced())
        return;

    if (strCommand == "mnget") { //Masternode Payments Request Sync
        if (fLiteMode)
            return; //disable all Masternode related functionality

        int nCountNeeded;
        vRecv >> nCountNeeded;

        if (Params().NetworkIDString() == CBaseChainParams::MAIN) {
            if (netfulfilledman.HasFulfilledRequest(pfrom->addr, "mnget")) {
                LogPrint(BCLog::NET, "mnget - peer already asked me for the list\n");
                Misbehaving(pfrom->GetId(), 20);
                return;
            }
        }
        netfulfilledman.AddFulfilledRequest(pfrom->addr, "mnget");
        masternodePayments.Sync(pfrom, nCountNeeded, *connman);
        LogPrint(BCLog::NET, "mnget - Sent Masternode winners to %s\n", pfrom->addr.ToString().c_str());
    } else if (strCommand == "mnw") { //Masternode Payments Declare Winner
        //this is required in litemodef
        CMasternodePaymentWinner winner;
        vRecv >> winner;

        if (pfrom->nVersion < MIN_MNW_PEER_PROTO_VERSION)
            return;

        int nHeight;
        {
            TRY_LOCK(cs_main, locked);
            if (!locked || ::ChainActive().Tip() == nullptr)
                return;
            nHeight = ::ChainActive().Tip()->nHeight;
        }

        if (masternodePayments.mapMasternodePayeeVotes.count(winner.GetHash())) {
            LogPrint(BCLog::NET, "mnw - Already seen - %s bestHeight %d\n", winner.GetHash().ToString().c_str(), nHeight);
            masternodeSync.AddedMasternodeWinner(winner.GetHash());
            return;
        }

        int nFirstBlock = nHeight - (mnodeman.CountEnabled() * 1.25);
        if (winner.nBlockHeight < nFirstBlock || winner.nBlockHeight > nHeight + 20) {
            LogPrint(BCLog::NET, "mnw - winner out of range - FirstBlock %d Height %d bestHeight %d\n", nFirstBlock, winner.nBlockHeight, nHeight);
            return;
        }

        std::string strError = "";
        if (!winner.IsValid(pfrom, strError, *connman)) {
            if (strError != "")
                LogPrint(BCLog::NET, "mnw - invalid message - %s\n", strError);
            return;
        }

        if (!masternodePayments.CanVote(winner.vinMasternode.prevout, winner.nBlockHeight)) {
            LogPrint(BCLog::NET, "mnw - masternode already voted - %s\n", winner.vinMasternode.prevout.ToStringShort());
            return;
        }

        if (!winner.SignatureValid()) {
            LogPrint(BCLog::NET, "mnw - invalid signature\n");
            if (masternodeSync.IsSynced())
                Misbehaving(pfrom->GetId(), 20);
            // it could just be a non-synced masternode
            mnodeman.AskForMN(pfrom, winner.vinMasternode, *connman);
            return;
        }

        LogPrint(BCLog::NET, "mnw - winning vote - Addr %s Height %d bestHeight %d - %s\n", EncodeDestination(ScriptHash(winner.payee)).c_str(), winner.nBlockHeight, nHeight, winner.vinMasternode.prevout.ToStringShort());

        if (masternodePayments.AddWinningMasternode(winner)) {
            winner.Relay(*connman);
            masternodeSync.AddedMasternodeWinner(winner.GetHash());
        }
    }
}

bool CMasternodePaymentWinner::Sign(CKey& keyMasternode, CPubKey& pubKeyMasternode)
{
    std::string errorMessage;
    std::string strMasterNodeSignMessage;

    std::string strMessage = vinMasternode.prevout.ToStringShort() + boost::lexical_cast<std::string>(nBlockHeight) + payee.ToString();

    if (!legacySigner.SignMessage(strMessage, errorMessage, vchSig, keyMasternode)) {
        LogPrint(BCLog::NET, "CMasternodePing::Sign() - Error: %s\n", errorMessage.c_str());
        return false;
    }

    if (!legacySigner.VerifyMessage(pubKeyMasternode, vchSig, strMessage, errorMessage)) {
        LogPrint(BCLog::NET, "CMasternodePing::Sign() - Error: %s\n", errorMessage.c_str());
        return false;
    }

    return true;
}

bool CMasternodePayments::GetBlockPayee(int nBlockHeight, CScript& payee)
{
    if (mapMasternodeBlocks.count(nBlockHeight)) {
        return mapMasternodeBlocks[nBlockHeight].GetPayee(payee);
    }

    return false;
}

// Is this masternode scheduled to get paid soon?
// -- Only look ahead up to 8 blocks to allow for propagation of the latest 2 winners
bool CMasternodePayments::IsScheduled(CMasternode& mn, int nNotBlockHeight)
{
    LOCK(cs_mapMasternodeBlocks);

    int nHeight;
    {
        TRY_LOCK(cs_main, locked);
        if (!locked || ::ChainActive().Tip() == nullptr)
            return false;
        nHeight = ::ChainActive().Tip()->nHeight;
    }

    CScript mnpayee;
    mnpayee = GetScriptForDestination(PKHash(mn.pubkey));

    CScript payee;
    for (int64_t h = nHeight; h <= nHeight + 8; h++) {
        if (h == nNotBlockHeight)
            continue;
        if (mapMasternodeBlocks.count(h)) {
            if (mapMasternodeBlocks[h].GetPayee(payee)) {
                if (mnpayee == payee) {
                    return true;
                }
            }
        }
    }

    return false;
}

bool CMasternodePayments::AddWinningMasternode(CMasternodePaymentWinner& winnerIn)
{
    uint256 blockHash = uint256();
    if (!GetBlockHash(blockHash, winnerIn.nBlockHeight - 100)) {
        return false;
    }

    {
        LOCK2(cs_mapMasternodePayeeVotes, cs_mapMasternodeBlocks);

        if (mapMasternodePayeeVotes.count(winnerIn.GetHash())) {
            return false;
        }

        mapMasternodePayeeVotes[winnerIn.GetHash()] = winnerIn;

        if (!mapMasternodeBlocks.count(winnerIn.nBlockHeight)) {
            CMasternodeBlockPayees blockPayees(winnerIn.nBlockHeight);
            mapMasternodeBlocks[winnerIn.nBlockHeight] = blockPayees;
        }
    }

    int n = 1;
    if (IsReferenceNode(winnerIn.vinMasternode))
        n = 100;
    mapMasternodeBlocks[winnerIn.nBlockHeight].AddPayee(winnerIn.payee, n);

    return true;
}

bool CMasternodeBlockPayees::IsTransactionValid(const CTransaction& txNew, const CAmount& nValueCreated)
{
    LOCK(cs_vecPayments);

    int nMaxSignatures = 0;
    std::string strPayeesPossible = "";

    CAmount masternodePayment = GetMasternodePayment(nBlockHeight, nValueCreated, Params().GetConsensus());

    //require at least 6 signatures

    for (const auto& payee : vecPayments)
        if (payee.nVotes >= nMaxSignatures && payee.nVotes >= MNPAYMENTS_SIGNATURES_REQUIRED)
            nMaxSignatures = payee.nVotes;

    // if we don't have at least 6 signatures on a payee, approve whichever is the longest chain
    if (nMaxSignatures < MNPAYMENTS_SIGNATURES_REQUIRED)
        return true;

    for (const auto& payee : vecPayments) {
        bool found = false;
        int pos = -1;
        for (unsigned int i = 0; i < txNew.vout.size(); i++) {
            if (payee.scriptPubKey == txNew.vout[i].scriptPubKey && masternodePayment == txNew.vout[i].nValue) {
                found = true;
                pos = i;
                break;
            }
        }

        if (payee.nVotes >= MNPAYMENTS_SIGNATURES_REQUIRED) {
            if (found) {
                //When proof of stake is active, enforce specific payment positions
                if (nBlockHeight >= Params().GetConsensus().PoSStartHeight() && pos != MN_PMT_SLOT)
                    return error("%s: Masternode payment is not in coinbase.vout[%d]", __func__, MN_PMT_SLOT);

                return true;
            }

            if (strPayeesPossible == "") {
                strPayeesPossible += EncodeDestination(ScriptHash(payee.scriptPubKey));
            } else {
                strPayeesPossible += "," + EncodeDestination(ScriptHash(payee.scriptPubKey));
            }
        }
    }

    LogPrint(BCLog::NET, "CMasternodePayments::IsTransactionValid - Missing required payment of %s to %s\n",
        FormatMoney(masternodePayment), strPayeesPossible.c_str());
    return false;
}

std::string CMasternodeBlockPayees::GetRequiredPaymentsString()
{
    LOCK(cs_vecPayments);

    std::string ret = "Unknown";

    for (const auto& payee : vecPayments) {
        if (ret != "Unknown") {
            ret += ", " + EncodeDestination(ScriptHash(payee.scriptPubKey)) + ":" + boost::lexical_cast<std::string>(payee.nVotes);
        } else {
            ret = EncodeDestination(ScriptHash(payee.scriptPubKey)) + ":" + boost::lexical_cast<std::string>(payee.nVotes);
        }
    }

    return ret;
}

std::string CMasternodePayments::GetRequiredPaymentsString(int nBlockHeight)
{
    LOCK(cs_mapMasternodeBlocks);

    if (mapMasternodeBlocks.count(nBlockHeight)) {
        return mapMasternodeBlocks[nBlockHeight].GetRequiredPaymentsString();
    }

    return "Unknown";
}

bool CMasternodePayments::IsTransactionValid(const CAmount& nValueCreated, const CTransaction& txNew, int nBlockHeight)
{
    LOCK(cs_mapMasternodeBlocks);

    if (mapMasternodeBlocks.count(nBlockHeight)) {
        return mapMasternodeBlocks[nBlockHeight].IsTransactionValid(txNew, nValueCreated);
    }

    return true;
}

void CMasternodePayments::CheckAndRemove()
{
    LOCK2(cs_mapMasternodePayeeVotes, cs_mapMasternodeBlocks);

    int nHeight;
    {
        TRY_LOCK(cs_main, locked);
        if (!locked || ::ChainActive().Tip() == nullptr)
            return;
        nHeight = ::ChainActive().Tip()->nHeight;
    }

    //keep up to five cycles for historical sake
    int nLimit = std::max(int(mnodeman.size() * 1.25), 1000);

    std::map<uint256, CMasternodePaymentWinner>::iterator it = mapMasternodePayeeVotes.begin();
    while (it != mapMasternodePayeeVotes.end()) {
        CMasternodePaymentWinner winner = (*it).second;

        if (nHeight - winner.nBlockHeight > nLimit) {
            LogPrint(BCLog::NET, "CMasternodePayments::CleanPaymentList - Removing old Masternode payment - block %d\n", winner.nBlockHeight);
            masternodeSync.mapSeenSyncMNW.erase((*it).first);
            mapMasternodePayeeVotes.erase(it++);
            mapMasternodeBlocks.erase(winner.nBlockHeight);
        } else {
            ++it;
        }
    }
}

bool IsReferenceNode(CTxIn& vin)
{
    //reference node - hybrid mode
    if (vin.prevout.ToStringShort() == "099c01bea63abd1692f60806bb646fa1d288e2d049281225f17e499024084e28-0")
        return true; // mainnet
    if (vin.prevout.ToStringShort() == "fbc16ae5229d6d99181802fd76a4feee5e7640164dcebc7f8feb04a7bea026f8-0")
        return true; // testnet
    if (vin.prevout.ToStringShort() == "e466f5d8beb4c2d22a314310dc58e0ea89505c95409754d0d68fb874952608cc-1")
        return true; // regtest

    return false;
}

bool CMasternodePaymentWinner::IsValid(CNode* pnode, std::string& strError, CConnman& connman)
{
    if (IsReferenceNode(vinMasternode))
        return true;

    CMasternode* pmn = mnodeman.Find(vinMasternode);

    if (!pmn) {
        strError = strprintf("Unknown Masternode %s", vinMasternode.prevout.ToStringShort());
        LogPrintf("CMasternodePaymentWinner::IsValid - %s\n", strError);
        mnodeman.AskForMN(pnode, vinMasternode, connman);
        return false;
    }

    if (pmn->protocolVersion < MIN_MNW_PEER_PROTO_VERSION) {
        strError = strprintf("Masternode protocol too old %d - req %d", pmn->protocolVersion, MIN_MNW_PEER_PROTO_VERSION);
        LogPrintf("CMasternodePaymentWinner::IsValid - %s\n", strError);
        return false;
    }

    int n = mnodeman.GetMasternodeRank(vinMasternode, nBlockHeight - 100, MIN_MNW_PEER_PROTO_VERSION);

    if (n > MNPAYMENTS_SIGNATURES_TOTAL) {
        //It's common to have masternodes mistakenly think they are in the top 10
        // We don't want to print all of these messages, or punish them unless they're way off
        if (n > MNPAYMENTS_SIGNATURES_TOTAL * 2) {
            strError = strprintf("Masternode not in the top %d (%d)", MNPAYMENTS_SIGNATURES_TOTAL, n);
            LogPrint(BCLog::NET, "CMasternodePaymentWinner::IsValid - %s\n", strError);
            if (masternodeSync.IsSynced())
                Misbehaving(pnode->GetId(), 20);
        }
        return false;
    }

    return true;
}

bool CMasternodePayments::ProcessBlock(int nBlockHeight, CConnman& connman)
{
    if (!fMasterNode)
        return false;

    //reference node - hybrid mode

    if (!IsReferenceNode(activeMasternode.vin)) {
        int n = mnodeman.GetMasternodeRank(activeMasternode.vin, nBlockHeight - 100, MIN_MNW_PEER_PROTO_VERSION);

        if (n == -1) {
            LogPrint(BCLog::NET, "CMasternodePayments::ProcessBlock - Unknown Masternode\n");
            return false;
        }

        if (n > MNPAYMENTS_SIGNATURES_TOTAL) {
            LogPrint(BCLog::NET, "CMasternodePayments::ProcessBlock - Masternode not in the top %d (%d)\n", MNPAYMENTS_SIGNATURES_TOTAL, n);
            return false;
        }
    }

    if (nBlockHeight <= nLastBlockHeight)
        return false;

    CMasternodePaymentWinner newWinner(activeMasternode.vin);

    if (budget.IsBudgetPaymentBlock(nBlockHeight)) {
        //is budget payment block -- handled by the budgeting software
    } else {
        LogPrint(BCLog::NET, "CMasternodePayments::ProcessBlock() Start nHeight %d - vin %s. \n", nBlockHeight, activeMasternode.vin.ToString().c_str());

        // pay to the oldest MN that still had no payment but its input is old enough and it was active long enough
        int nCount = 0;
        CMasternode* pmn = mnodeman.GetNextMasternodeInQueueForPayment(nBlockHeight, true, nCount);

        if (pmn != nullptr) {
            LogPrint(BCLog::NET, "CMasternodePayments::ProcessBlock() Found by FindOldestNotInVec \n");

            newWinner.nBlockHeight = nBlockHeight;

            CScript payee = GetScriptForDestination(PKHash(pmn->pubkey));
            newWinner.AddPayee(payee);

            LogPrint(BCLog::NET, "CMasternodePayments::ProcessBlock() Winner payee %s nHeight %d. \n", EncodeDestination(ScriptHash(payee)).c_str(), newWinner.nBlockHeight);
        } else {
            LogPrint(BCLog::NET, "CMasternodePayments::ProcessBlock() Failed to find masternode to pay\n");
        }
    }

    std::string errorMessage;
    CPubKey pubKeyMasternode;
    CKey keyMasternode;

    if (!legacySigner.SetKey(strMasterNodePrivKey, keyMasternode, pubKeyMasternode)) {
        LogPrint(BCLog::NET, "CMasternodePayments::ProcessBlock() - Error upon calling SetKey: %s\n", errorMessage.c_str());
        return false;
    }

    LogPrint(BCLog::NET, "CMasternodePayments::ProcessBlock() - Signing Winner\n");
    if (newWinner.Sign(keyMasternode, pubKeyMasternode)) {
        LogPrint(BCLog::NET, "CMasternodePayments::ProcessBlock() - AddWinningMasternode\n");

        if (AddWinningMasternode(newWinner)) {
            newWinner.Relay(connman);
            nLastBlockHeight = nBlockHeight;
            return true;
        }
    }

    return false;
}

void CMasternodePaymentWinner::Relay(CConnman& connman)
{
    CInv inv(MSG_MASTERNODE_WINNER, GetHash());
    connman.RelayInv(inv);
}

bool CMasternodePaymentWinner::SignatureValid()
{

    CMasternode* pmn = mnodeman.Find(vinMasternode);

    if (pmn != nullptr) {
        std::string strMessage = vinMasternode.prevout.ToStringShort() + boost::lexical_cast<std::string>(nBlockHeight) + payee.ToString();

        std::string errorMessage = "";
        if (!legacySigner.VerifyMessage(pmn->pubkey2, vchSig, strMessage, errorMessage)) {
            return error("CMasternodePaymentWinner::SignatureValid() - Got bad Masternode address signature %s \n", vinMasternode.ToString().c_str());
        }

        return true;
    }

    return false;
}

void CMasternodePayments::Sync(CNode* node, int nCountNeeded, CConnman& connman)
{
    LOCK(cs_mapMasternodePayeeVotes);

    int nHeight;
    {
        TRY_LOCK(cs_main, locked);
        if (!locked || ::ChainActive().Tip() == nullptr)
            return;
        nHeight = ::ChainActive().Tip()->nHeight;
    }

    int nCount = (mnodeman.CountEnabled() * 1.25);
    if (nCountNeeded > nCount)
        nCountNeeded = nCount;

    int nInvCount = 0;
    std::map<uint256, CMasternodePaymentWinner>::iterator it = mapMasternodePayeeVotes.begin();
    while (it != mapMasternodePayeeVotes.end()) {
        CMasternodePaymentWinner winner = (*it).second;
        if (winner.nBlockHeight >= nHeight - nCountNeeded && winner.nBlockHeight <= nHeight + 20) {
            node->PushInventory(CInv(MSG_MASTERNODE_WINNER, winner.GetHash()));
            nInvCount++;
        }
        ++it;
    }

    const CNetMsgMaker msgMaker(PROTOCOL_VERSION);
    connman.PushMessage(node, msgMaker.Make("ssc", MASTERNODE_SYNC_MNW, nInvCount));
}

std::string CMasternodePayments::ToString() const
{
    std::ostringstream info;

    info << "Votes: " << (int)mapMasternodePayeeVotes.size() << ", Blocks: " << (int)mapMasternodeBlocks.size();

    return info.str();
}

int CMasternodePayments::GetOldestBlock()
{
    LOCK(cs_mapMasternodeBlocks);

    int nOldestBlock = std::numeric_limits<int>::max();

    std::map<int, CMasternodeBlockPayees>::iterator it = mapMasternodeBlocks.begin();
    while (it != mapMasternodeBlocks.end()) {
        if ((*it).first < nOldestBlock) {
            nOldestBlock = (*it).first;
        }
        it++;
    }

    return nOldestBlock;
}

int CMasternodePayments::GetNewestBlock()
{
    LOCK(cs_mapMasternodeBlocks);

    int nNewestBlock = 0;

    std::map<int, CMasternodeBlockPayees>::iterator it = mapMasternodeBlocks.begin();
    while (it != mapMasternodeBlocks.end()) {
        if ((*it).first > nNewestBlock) {
            nNewestBlock = (*it).first;
        }
        it++;
    }

    return nNewestBlock;
}
