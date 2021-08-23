// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2014-2018 The Crown developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <crown/legacysigner.h>

#include <index/txindex.h>
#include <init.h>
#include <masternode/masternodeman.h>
#include <node/context.h>
#include <rpc/blockchain.h>
#include <rpc/util.h>
#include <script/sign.h>
#include <shutdown.h>
#include <util/message.h>
#include <util/system.h>
#include <util/time.h>
#include <wallet/rpcwallet.h>
#include <wallet/wallet.h>
#include <key_io.h>

#include <boost/algorithm/string/replace.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/lexical_cast.hpp>

#include <algorithm>
#include <boost/assign/list_of.hpp>

using namespace std;
using namespace boost;

CLegacySigner legacySigner;

bool CLegacySigner::IsVinAssociatedWithPubkey(CTxIn& vin, CPubKey& pubkey, const Consensus::Params& consensusParams)
{
    // Return true if txindex isnt ready
    if (!g_txindex->IsTxIndexSynced())
        return true;

    uint256 hash;
    CTransactionRef txVin;
    g_txindex->BlockUntilSyncedToCurrentChain();
    CScript payee = GetScriptForDestination(PKHash(pubkey));

    //! get tx from disk
    if (!g_txindex->FindTx(vin.prevout.hash, hash, txVin)) {
        LogPrintf("%s - could not retrieve tx %s\n", __func__, vin.prevout.hash.ToString());
        return false;
    }

    //! get specific vout
    unsigned int voutn = 0;
    for (const auto& out : txVin->vout) {
        if (vin.prevout.n != voutn++)
            continue;
        // LogPrintf("     vout %d - found %s - expected %s\n", voutn, out.scriptPubKey.ToString(), payee.ToString());
        if (out.scriptPubKey == payee) {
            if (out.nValue == consensusParams.nMasternodeCollateral) {
                return true;
            }
            LogPrintf("     pubkeys match but value incorrect (expecting %llu, got %llu)\n", consensusParams.nMasternodeCollateral, out.nValue);
        }
    }

    LogPrintf("%s - vin not associated with given pubkey (vin: %s)\n", __func__, vin.ToString());
    return false;
}

bool CLegacySigner::SetCollateralAddress(std::string strAddress)
{
    CTxDestination dest = DecodeDestination(strAddress);
    if (!IsValidDestination(dest))
        return false;
    collateralPubKey = GetScriptForDestination(dest);
    return true;
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

bool CLegacySigner::SignMessage(const std::string& strMessage, std::vector<unsigned char>& vchSigRet, const CKey& key)
{
    CHashWriter ss(SER_GETHASH, 0);
    ss << MESSAGE_MAGIC;
    ss << strMessage;

    return CHashSigner::SignHash(ss.GetHash(), key, vchSigRet);
}

bool CLegacySigner::VerifyMessage(const CPubKey& pubkey, const std::vector<unsigned char>& vchSig, const std::string& strMessage, std::string& strErrorRet)
{
    return VerifyMessage(pubkey.GetID(), vchSig, strMessage, strErrorRet);
}

bool CLegacySigner::VerifyMessage(const CKeyID& keyID, const std::vector<unsigned char>& vchSig, const std::string& strMessage, std::string& strErrorRet)
{
    CHashWriter ss(SER_GETHASH, 0);
    ss << MESSAGE_MAGIC;
    ss << strMessage;

    return CHashSigner::VerifyHash(ss.GetHash(), keyID, vchSig, strErrorRet);
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

    return true;
}
