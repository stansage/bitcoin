// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2020 The Bitcoin Core developers
// Copyright (c) 2020 The Crown developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <crown/nodewallet.h>

bool CWallet::GetMasternodeVinAndKeys(CTxIn& txinRet, CPubKey& pubKeyRet, CKey& keyRet, std::string strTxHash, std::string strOutputIndex)
{
    LOCK(cs_wallet);
    std::vector<COutput> vPossibleCoins;
    AvailableCoins(vPossibleCoins, true, nullptr, Params().GetConsensus().nMasternodeCollateral, Params().GetConsensus().nMasternodeCollateral);
    if (vPossibleCoins.empty()) {
        LogPrintf("CWallet::GetMasternodeVinAndKeys -- Could not locate any valid masternode vin\n");
        return false;
    }

    uint256 txHash = uint256S(strTxHash);
    int nOutputIndex = atoi(strOutputIndex.c_str());

    for (COutput& out : vPossibleCoins) {
        if (out.tx->GetHash() == txHash && out.i == nOutputIndex) {
            return GetVinAndKeysFromOutput(out, txinRet, pubKeyRet, keyRet);
        }
    }

    LogPrintf("CWallet::GetMasternodeVinAndKeys - Could not locate specified masternode vin\n");
    return false;
}

bool CWallet::GetVinAndKeysFromOutput(COutput out, CTxIn& txinRet, CPubKey& pubkeyRet, CKey& keyRet)
{
    CScript pubScript;
    CKeyID keyID;

    txinRet = CTxIn(out.tx->tx->GetHash(), out.i);
    pubScript = out.tx->tx->vout[out.i].scriptPubKey;

    CTxDestination address;
    ExtractDestination(pubScript, address);
    auto key_id = boost::get<PKHash>(&address);
    keyID = ToKeyID(*key_id);
    if (!key_id) {
        LogPrintf("GetVinFromOutput -- Address does not refer to a key\n");
        return false;
    }

    LegacyScriptPubKeyMan* spk_man = GetLegacyScriptPubKeyMan();
    if (!spk_man) {
        LogPrintf("GetVinFromOutput -- This type of wallet does not support this command\n");
        return false;
    }

    if (!spk_man->GetKey(keyID, keyRet)) {
        LogPrintf("GetVinFromOutput -- Private key for address is not known\n");
        return false;
    }

    pubkeyRet = keyRet.GetPubKey();
    return true;
}

bool CWallet::GetBudgetSystemCollateralTX(CTransactionRef& tx, uint256 hash)
{
    const CAmount BUDGET_FEE_TX = (25 * COIN);

    CScript scriptChange;
    scriptChange << OP_RETURN << ToByteVector(hash);

    std::vector<CRecipient> vecSend;
    vecSend.push_back((CRecipient) { scriptChange, BUDGET_FEE_TX, false });

    CCoinControl coinControl;
    int nChangePosRet = -1;
    CAmount nFeeRequired = 0;
    bilingual_str error;
    FeeCalculation fee_calc_out;

    return CreateTransaction(vecSend, tx, nFeeRequired, nChangePosRet, error, coinControl, fee_calc_out);
}

bool CWallet::GetActiveMasternode(CMasternode*& activeStakingNode)
{
    activeStakingNode = nullptr;
    if (activeMasternode.status == ACTIVE_MASTERNODE_STARTED)
        activeStakingNode = mnodeman.Find(activeMasternode.vin);
    return activeStakingNode != nullptr;
}


void NodeMinter(const CChainParams& chainparams, CConnman& connman)
{
    util::ThreadRename("crown-minter");

    auto pwallet = GetMainWallet();
    if (!pwallet)
        return;

    if (ShutdownRequested())
        return;
    if (!fMasterNode)
        return;
    if (fReindex || fImporting || pwallet->IsLocked())
        return;
    if (!gArgs.GetBoolArg("-jumpstart", false)) {
        if (connman.GetNodeCount(CConnman::CONNECTIONS_ALL) == 0 ||
            ::ChainActive().Tip()->nHeight < chainparams.GetConsensus().nLastPOWBlock ||
            ::ChainstateActive().IsInitialBlockDownload() ||
            !masternodeSync.IsSynced()) {
                return;
        }
    }

    LogPrintf("%s: Attempting to stake..\n", __func__);

//    unsigned int nExtraNonce = 0;

//    CScript coinbaseScript;
//    GetScriptForMining(coinbaseScript, pwallet);
//    if (coinbaseScript.empty()) return;

//    //
//    // Create new block
//    //
//    CBlockIndex* pindexPrev = ::ChainActive().Tip();
//    if (!pindexPrev) return;

//    BlockAssembler assembler(*g_rpc_node->mempool, chainparams);
//    auto pblocktemplate = assembler.CreateNewBlock(coinbaseScript, pwallet.get(), true);
//    if (!pblocktemplate.get()) {
//        LogPrintf("%s: Stake not found..\n", __func__);
//        return;
//    }

//    auto pblock = std::make_shared<CBlock>(pblocktemplate->block);
//    IncrementExtraNonce(pblock.get(), pindexPrev, nExtraNonce);

//    // sign block
//    LogPrintf("CPUMiner : proof-of-stake block found %s\n", pblock->GetHash().ToString());
//    if (!SignBlock(pblock.get())) {
//        LogPrintf("%s: SignBlock failed", __func__);
//        return;
//    }
//    LogPrintf("%s : proof-of-stake block was signed %s\n", __func__, pblock->GetHash().ToString());

//    // check if block is valid
//    BlockValidationState state;
//    if (!TestBlockValidity(state, chainparams, *pblock, pindexPrev, false, false)) {
//        LogPrintf("%s: TestBlockValidity failed: %s", __func__, state.ToString());
//        return;
//    }

//    //! guts of ProcessBlockFound()
//    if (pblock->hashPrevBlock != ::ChainActive().Tip()->GetBlockHash()) {
//        LogPrintf("%s - generated block is stale\n", __func__);
//        return;
//    } else {
//        LOCK(cs_main);
//        if (!g_chainman.ProcessNewBlock(chainparams, pblock, true, nullptr)) {
//            LogPrintf("%s - ProcessNewBlock() failed, block not accepted\n", __func__);
//            return;
//        }
//    }

    return;
}
