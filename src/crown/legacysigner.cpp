// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2014-2018 The Crown developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <crown/legacysigner.h>

#include <crown/instantx.h>
#include <init.h>
#include <masternode/masternodeman.h>
#include <node/context.h>
#include <rpc/blockchain.h>
#include <rpc/util.h>
#include <script/sign.h>
#include <systemnode/systemnodeman.h>
#include <util/message.h>
#include <util/system.h>
#include <util/time.h>
#include <wallet/rpcwallet.h>
#include <wallet/wallet.h>

#include <boost/algorithm/string/replace.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/lexical_cast.hpp>

#include <algorithm>
#include <boost/assign/list_of.hpp>
#include <openssl/rand.h>

using namespace std;
using namespace boost;

// A helper object for signing messages from Masternodes
CLegacySigner legacySigner;

// Keep track of the active Masternode
CActiveMasternode activeMasternode;

bool CLegacySigner::SetCollateralAddress(std::string strAddress)
{
    CTxDestination dest = DecodeDestination(strAddress);
    if (!IsValidDestination(dest))
        return false;
    collateralPubKey = GetScriptForDestination(dest);
    return true;
}

bool CLegacySigner::IsVinAssociatedWithPubkey(CTxIn& vin, CPubKey& pubkey)
{
    uint256 hash;
    CScript payee2 = GetScriptForDestination(PKHash(pubkey));
    const CTransactionRef txVin = GetTransaction(::ChainActive().Tip(), nullptr, vin.prevout.hash, Params().GetConsensus(), hash);
    if (txVin) {
        for (CTxOut out : txVin->vout) {
            if (out.nValue == Params().GetConsensus().MasternodeCollateral()) {
                if (out.scriptPubKey == payee2)
                    return true;
            }
        }
    }

    return false;
}

bool CLegacySigner::SetKey(std::string strSecret, CKey& key, CPubKey& pubkey)
{
    auto m_wallet = GetMainWallet();
    EnsureLegacyScriptPubKeyMan(*m_wallet, true);

    key = DecodeSecret(strSecret);
    if (!key.IsValid())
        return false;
    pubkey = key.GetPubKey();

    return true;
}

bool CLegacySigner::SignMessage(std::string strMessage, std::string& errorMessage, vector<unsigned char>& vchSig, CKey key)
{
    CHashWriter ss(SER_GETHASH, 0);
    ss << MESSAGE_MAGIC;
    ss << strMessage;

    if (!key.SignCompact(ss.GetHash(), vchSig)) {
        errorMessage = ("Signing failed.");
        return false;
    }

    return true;
}

bool CLegacySigner::VerifyMessage(CPubKey pubkey, const vector<unsigned char>& vchSig, std::string strMessage, std::string& errorMessage)
{
    CHashWriter ss(SER_GETHASH, 0);
    ss << MESSAGE_MAGIC;
    ss << strMessage;

    CPubKey pubkey2;
    if (!pubkey2.RecoverCompact(ss.GetHash(), vchSig)) {
        errorMessage = ("Error recovering public key.");
        return false;
    }

    auto verifyResult = PKHash(pubkey2) == PKHash(pubkey);
    if (!verifyResult)
        LogPrintf("CLegacySigner::VerifyMessage -- keys don't match: %s %s (called by %s)\n", pubkey2.GetID().ToString(), pubkey.GetID().ToString());
    else
        LogPrintf("CLegacySigner::VerifyMessage -- keys match: %s %s (called by %s)\n", pubkey2.GetID().ToString(), pubkey.GetID().ToString());

    return verifyResult;
}

//TODO: Rename/move to core
void ThreadCheckLegacySigner()
{
    // Make this thread recognisable as the wallet flushing thread
    util::ThreadRename("crown-legacysigner");

    unsigned int c1 = 0;
    unsigned int c2 = 0;

    while (true) {
        UninterruptibleSleep(std::chrono::microseconds(1000));
        LogPrintf("ThreadCheckLegacySigner::check timeout\n");

        // try to sync from all available nodes, one step at a time
        masternodeSync.Process(*g_rpc_node->connman);
        systemnodeSync.Process(*g_rpc_node->connman);

        if (masternodeSync.IsBlockchainSynced()) {

            c1++;

            // check if we should activate or ping every few minutes,
            // start right after sync is considered to be done
            if (c1 % MASTERNODE_PING_SECONDS == 15)
                activeMasternode.ManageStatus(*g_rpc_node->connman);

            if (c1 % 60 == 0) {
                mnodeman.CheckAndRemove();
                mnodeman.ProcessMasternodeConnections(*g_rpc_node->connman);
                masternodePayments.CheckAndRemove();
                instantSend.CheckAndRemove();
            }
        }

        if (systemnodeSync.IsBlockchainSynced()) {

            c2++;

            // check if we should activate or ping every few minutes,
            // start right after sync is considered to be done
            if (c2 % SYSTEMNODE_PING_SECONDS == 15)
                activeSystemnode.ManageStatus(*g_rpc_node->connman);

            if (c2 % 60 == 0) {
                snodeman.CheckAndRemove();
                snodeman.ProcessSystemnodeConnections(*g_rpc_node->connman);
                systemnodePayments.CheckAndRemove();
                instantSend.CheckAndRemove();
            }
        }
    }
}

bool CHashSigner::SignHash(const uint256& hash, const CKey& key, std::vector<unsigned char>& vchSigRet)
{
    return key.SignCompact(hash, vchSigRet);
}

bool CHashSigner::VerifyHash(const uint256& hash, const CPubKey& pubkey, const std::vector<unsigned char>& vchSig, std::string& strErrorRet)
{
    return VerifyHash(hash, pubkey.GetID(), vchSig, strErrorRet);
}

bool CHashSigner::VerifyHash(const uint256& hash, const CKeyID& keyID, const std::vector<unsigned char>& vchSig, std::string& strErrorRet)
{
    CPubKey pubkeyFromSig;
    if (!pubkeyFromSig.RecoverCompact(hash, vchSig)) {
        strErrorRet = "Error recovering public key.";
        return false;
    }

    if (pubkeyFromSig.GetID() != keyID) {
        strErrorRet = strprintf("Keys don't match: pubkey=%s, pubkeyFromSig=%s", keyID.ToString(), pubkeyFromSig.GetID().ToString());
        return false;
    }

    return true;
}
