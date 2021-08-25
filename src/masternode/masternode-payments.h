// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2014-2018 The Crown developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MASTERNODE_PAYMENTS_H
#define MASTERNODE_PAYMENTS_H

#include <boost/lexical_cast.hpp>
#include <key.h>
#include <key_io.h>
#include <validation.h>

#define MNPAYMENTS_SIGNATURES_TOTAL 10
using namespace std;

extern RecursiveMutex cs_mapMasternodePayeeVotes;

class CNode;
class CMasternodePaymentWinner;
extern std::map<uint256, CMasternodePaymentWinner> mapMasternodePayeeVotes;

bool IsReferenceNode(CTxIn& vin);

// for storing the winning payments
class CMasternodePaymentWinner {
public:
    CTxIn vinMasternode;

    int nBlockHeight;
    CScript payee;
    std::vector<unsigned char> vchSig;

    CMasternodePaymentWinner()
    {
        nBlockHeight = 0;
        vinMasternode = CTxIn();
        payee = CScript();
    }

    CMasternodePaymentWinner(CTxIn vinIn)
    {
        nBlockHeight = 0;
        vinMasternode = vinIn;
        payee = CScript();
    }

    uint256 GetHash()
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << *(CScriptBase*)(&payee);
        ss << nBlockHeight;
        ss << vinMasternode.prevout;

        return ss.GetHash();
    }

    bool Sign(CKey& keyMasternode, CPubKey& pubKeyMasternode);
    bool IsValid(CNode* pnode, std::string& strError, CConnman& connman);
    bool SignatureValid();
    void Relay(CConnman& connman);

    void AddPayee(CScript payeeIn)
    {
        payee = payeeIn;
    }

    SERIALIZE_METHODS(CMasternodePaymentWinner, obj)
    {
        READWRITE(obj.vinMasternode);
        READWRITE(obj.nBlockHeight);
        READWRITE(*(CScriptBase*)(&obj.payee));
        READWRITE(obj.vchSig);
    }

    std::string ToString()
    {
        std::string ret = "";
        ret += vinMasternode.ToString();
        ret += ", " + boost::lexical_cast<std::string>(nBlockHeight);
        ret += ", " + payee.ToString();
        ret += ", " + boost::lexical_cast<std::string>((int)vchSig.size());
        return ret;
    }
};

    bool AddWinningMasternode(CMasternodePaymentWinner& winner);
    std::string GetRequiredPaymentsString(int nBlockHeight);

#endif
