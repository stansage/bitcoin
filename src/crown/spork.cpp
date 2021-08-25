//#include <consensus/validation.h>
//#include <crown/spork.h>
//#include <masternode/masternode-budget.h>
//#include <net_processing.h>
//#include <netmessagemaker.h>
//#include <node/context.h>
//#include <rpc/blockchain.h>

//using namespace std;
//using namespace boost;

//class CSporkMessage;
//class CSporkManager;

//CSporkManager sporkManager;
//std::map<uint256, CSporkMessage> mapSporks;
//std::map<int, CSporkMessage> mapSporksActive;

//void ProcessSpork(CNode* pfrom, CConnman* connman, const std::string& strCommand, CDataStream& vRecv)
//{
//    if (strCommand == NetMsgType::SPORK) {
//        CDataStream vMsg(vRecv);
//        CSporkMessage spork;
//        vRecv >> spork;

//        if (::ChainActive().Tip() == NULL)
//            return;

//        uint256 hash = spork.GetHash();
//        if (mapSporksActive.count(spork.nSporkID)) {
//            if (mapSporksActive[spork.nSporkID].nTimeSigned >= spork.nTimeSigned) {
//                LogPrintf("spork - seen %s block %d \n", hash.ToString(), ::ChainActive().Tip()->nHeight);
//                return;
//            } else {
//                LogPrintf("spork - got updated spork %s block %d \n", hash.ToString(), ::ChainActive().Tip()->nHeight);
//            }
//        }

//        LogPrintf("spork - new %s ID %d Time %d bestHeight %d\n", hash.ToString(), spork.nSporkID, spork.nValue, ::ChainActive().Tip()->nHeight);

//        if (!sporkManager.CheckSignature(spork)) {
//            LogPrintf("spork - invalid signature\n");
//            Misbehaving(pfrom->GetId(), 100);
//            return;
//        }

//        mapSporks[hash] = spork;
//        mapSporksActive[spork.nSporkID] = spork;
//        sporkManager.Relay(spork, *connman);

//        //does a task if needed
//        ExecuteSpork(spork.nSporkID, spork.nValue, *connman);
//    }
//    if (strCommand == NetMsgType::GETSPORKS) {
//        std::map<int, CSporkMessage>::iterator it = mapSporksActive.begin();

//        while (it != mapSporksActive.end()) {
//            const CNetMsgMaker msgMaker(PROTOCOL_VERSION);
//            connman->PushMessage(pfrom, msgMaker.Make(NetMsgType::SPORK, it->second));
//            it++;
//        }
//    }
//}

//// grab the spork, otherwise say it's off
//bool IsSporkActive(int nSporkID)
//{
//    int64_t r = -1;

//    if (mapSporksActive.count(nSporkID)) {
//        r = mapSporksActive[nSporkID].nValue;
//    } else {
//        if (nSporkID == SPORK_1_ENABLE_MASTERNODE_PAYMENTS)
//            r = SPORK_1_ENABLE_MASTERNODE_PAYMENTS_DEFAULT;
//        if (nSporkID == SPORK_2_MASTERNODE_PAYMENT_ENFORCEMENT)
//            r = SPORK_2_MASTERNODE_PAYMENT_ENFORCEMENT_DEFAULT;
////        if (nSporkID == SPORK_3_RESET_BUDGET)
////            r = SPORK_3_RESET_BUDGET_DEFAULT;

//        if (r == -1)
//            LogPrintf("GetSpork::Unknown Spork %d\n", nSporkID);
//    }
//    if (r == -1)
//        r = 4070908800; //return 2099-1-1 by default

//    return r < GetTime();
//}

//// grab the value of the spork on the network, or the default
//int64_t GetSporkValue(int nSporkID)
//{
//    int64_t r = -1;

//    if (mapSporksActive.count(nSporkID)) {
//        r = mapSporksActive[nSporkID].nValue;
//    } else {
//        if (nSporkID == SPORK_1_ENABLE_MASTERNODE_PAYMENTS)
//            r = SPORK_1_ENABLE_MASTERNODE_PAYMENTS_DEFAULT;
//        if (nSporkID == SPORK_2_MASTERNODE_PAYMENT_ENFORCEMENT)
//            r = SPORK_2_MASTERNODE_PAYMENT_ENFORCEMENT_DEFAULT;
////        if (nSporkID == SPORK_3_RESET_BUDGET)
////            r = SPORK_3_RESET_BUDGET_DEFAULT;

//        if (r == -1)
//            LogPrintf("GetSpork::Unknown Spork %d\n", nSporkID);
//    }

//    return r;
//}

//void ExecuteSpork(int nSporkID, int nValue, CConnman& connman)
//{
////    if (nSporkID == SPORK_3_RESET_BUDGET && nValue == 1) {
////        budget.Clear();
////    }
//    LogPrintf("Spork::ExecuteSpork -- %s(%d) %d\n", sporkManager.GetSporkNameByID(nSporkID), nSporkID, nValue);
//}

//bool CSporkManager::CheckSignature(CSporkMessage& spork)
//{
//    CPubKey pubkey(ParseHex(Params().GetConsensus().SporkKey()));
//    std::string strMessage = std::to_string(spork.nSporkID) + std::to_string(spork.nValue) + std::to_string(spork.nTimeSigned);
//    std::string strError = "";

//    if (!legacySigner.VerifyMessage(pubkey, spork.vchSig, strMessage, strError)) {
//        LogPrintf("CSporkMessage::CheckSignature -- VerifyHash() failed, error: %s\n", strError);
//        return false;
//    }

//    return true;
//}

//bool CSporkManager::Sign(CSporkMessage& spork)
//{
//    std::string strMessage = boost::lexical_cast<std::string>(spork.nSporkID) + boost::lexical_cast<std::string>(spork.nValue) + boost::lexical_cast<std::string>(spork.nTimeSigned);

//    CKey key2;
//    CPubKey pubkey2;
//    std::string errorMessage = "";

//    if (!legacySigner.SetKey(strMasterPrivKey, key2, pubkey2)) {
//        LogPrintf("CMasternodePayments::Sign - ERROR: Invalid masternodeprivkey: '%s'\n", errorMessage);
//        return false;
//    }

//    if (!legacySigner.SignMessage(strMessage, spork.vchSig, key2)) {
//        LogPrintf("CMasternodePayments::Sign - Sign message failed");
//        return false;
//    }

//    if (!legacySigner.VerifyMessage(pubkey2, spork.vchSig, strMessage, errorMessage)) {
//        LogPrintf("CMasternodePayments::Sign - Verify message failed");
//        return false;
//    }

//    return true;
//}

//bool CSporkManager::UpdateSpork(int nSporkID, int64_t nValue, CConnman& connman)
//{
//    CSporkMessage msg;
//    msg.nSporkID = nSporkID;
//    msg.nValue = nValue;
//    msg.nTimeSigned = GetTime();

//    if (Sign(msg)) {
//        CInv spork(MSG_SPORK, msg.GetHash());
//        connman.RelayInv(spork);
//        mapSporks[msg.GetHash()] = msg;
//        mapSporksActive[nSporkID] = msg;
//        return true;
//    }

//    return false;
//}

//void CSporkManager::Relay(CSporkMessage& msg, CConnman& connman)
//{
//    CInv inv(MSG_SPORK, msg.GetHash());
//    connman.RelayInv(inv);
//}

//bool CSporkManager::SetPrivKey(std::string strPrivKey)
//{
//    CSporkMessage msg;

//    // Test signing successful, proceed
//    strMasterPrivKey = strPrivKey;

//    Sign(msg);

//    if (CheckSignature(msg)) {
//        LogPrintf("CSporkManager::SetPrivKey - Successfully initialized as spork signer\n");
//        return true;
//    } else {
//        return false;
//    }
//}

//int CSporkManager::GetSporkIDByName(std::string strName)
//{
//    if (strName == "SPORK_1_ENABLE_MASTERNODE_PAYMENTS")
//        return SPORK_1_ENABLE_MASTERNODE_PAYMENTS;
//    if (strName == "SPORK_2_MASTERNODE_PAYMENT_ENFORCEMENT")
//        return SPORK_2_MASTERNODE_PAYMENT_ENFORCEMENT;
////    if (strName == "SPORK_3_RESET_BUDGET")
////        return SPORK_3_RESET_BUDGET;
//    return -1;
//}

//std::string CSporkManager::GetSporkNameByID(int id)
//{
//    if (id == SPORK_1_ENABLE_MASTERNODE_PAYMENTS)
//        return "SPORK_1_ENABLE_MASTERNODE_PAYMENTS";
//    if (id == SPORK_2_MASTERNODE_PAYMENT_ENFORCEMENT)
//        return "SPORK_2_MASTERNODE_PAYMENT_ENFORCEMENT";
////    if (id == SPORK_3_RESET_BUDGET)
////        return "SPORK_3_RESET_BUDGET";

//    return "Unknown";
//}
