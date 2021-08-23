// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2014-2018 The Crown developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <crown/spork.h>
#include <masternode/masternode-payments.h>
#include <mn_processing.h>
#include <net_processing.h>
#include <netfulfilledman.h>
#include <netmessagemaker.h>
#include <util/moneystr.h>

std::map<uint256, CMasternodePaymentWinner> mapMasternodePayeeVotes;


bool AddWinningMasternode(CMasternodePaymentWinner& winnerIn)
{
    //!TODO: implement AddWinningMasternode
    uint256 blockHash = uint256();
    if (!GetBlockHash(blockHash, winnerIn.nBlockHeight - 100)) {
        return false;
    }

    {
        if (mapMasternodePayeeVotes.count(winnerIn.GetHash())) {
            return false;
        }

//        LOCK2(cs_mapMasternodeBlocks, cs_mapMasternodePayeeVotes);

        mapMasternodePayeeVotes[winnerIn.GetHash()] = winnerIn;

//        if (!mapMasternodeBlocks.count(winnerIn.nBlockHeight)) {
//            CMasternodeBlockPayees blockPayees(winnerIn.nBlockHeight);
//            mapMasternodeBlocks[winnerIn.nBlockHeight] = blockPayees;
//        }
    }

    int n = 1;
    if (IsReferenceNode(winnerIn.vinMasternode))
        n = 100;
//    mapMasternodeBlocks[winnerIn.nBlockHeight].AddPayee(winnerIn.payee, n);

    return true;
}

std::string GetRequiredPaymentsString(int nBlockHeight)
{
//!TODO: implement GetRequiredPaymentsString
//    LOCK(cs_mapMasternodeBlocks);

//    if (mapMasternodeBlocks.count(nBlockHeight)) {
//        return mapMasternodeBlocks[nBlockHeight].GetRequiredPaymentsString();
//    }

    return "Unknown" + std::to_string(nBlockHeight);
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
        LogPrint(BCLog::MASTERNODE, "CMasternodePaymentWinner::IsValid - %s\n", strError);
        mnodeman.AskForMN(pnode, vinMasternode, connman);
        return false;
    }

    if (pmn->protocolVersion < MIN_MNW_PEER_PROTO_VERSION) {
        strError = strprintf("Masternode protocol too old %d - req %d", pmn->protocolVersion, MIN_MNW_PEER_PROTO_VERSION);
        LogPrint(BCLog::MASTERNODE, "CMasternodePaymentWinner::IsValid - %s\n", strError);
        return false;
    }

    int n = mnodeman.GetMasternodeRank(vinMasternode, nBlockHeight - 100, MIN_MNW_PEER_PROTO_VERSION);

    if (n > MNPAYMENTS_SIGNATURES_TOTAL) {
        //It's common to have masternodes mistakenly think they are in the top 10
        // We don't want to print all of these messages, or punish them unless they're way off
        if (n > MNPAYMENTS_SIGNATURES_TOTAL * 2) {
            strError = strprintf("Masternode not in the top %d (%d)", MNPAYMENTS_SIGNATURES_TOTAL, n);
            LogPrint(BCLog::MASTERNODE, "CMasternodePaymentWinner::IsValid - %s\n", strError);
            if (masternodeSync.IsSynced())
                Misbehaving(pnode->GetId(), 20);
        }
        return false;
    }

    return true;
}

void CMasternodePaymentWinner::Relay(CConnman& connman)
{
    CInv inv(MSG_MASTERNODE_WINNER, GetHash());
    connman.RelayInv(inv);
}

bool CMasternodePaymentWinner::SignatureValid()
{

    CMasternode* pmn = mnodeman.Find(vinMasternode);

    if (pmn) {
        std::string strMessage = vinMasternode.prevout.ToStringShort() + boost::lexical_cast<std::string>(nBlockHeight) + payee.ToString();

        std::string errorMessage = "";
        if (!legacySigner.VerifyMessage(pmn->pubkey2, vchSig, strMessage, errorMessage)) {
            return error("CMasternodePaymentWinner::SignatureValid() - Got bad Masternode address signature %s \n", vinMasternode.ToString().c_str());
        }

        return true;
    }

    return false;
}

