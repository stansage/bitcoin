// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2014-2018 The Crown developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MASTERNODE_H
#define MASTERNODE_H

#include <arith_uint256.h>
#include <base58.h>
#include <crown/legacycalls.h>
#include <key.h>
#include <net.h>
#include <sync.h>
#include <timedata.h>
#include <util/system.h>
#include <validation.h>

#define MASTERNODE_MIN_CONFIRMATIONS 15
#define MASTERNODE_MIN_MNP_SECONDS (10 * 60)
#define MASTERNODE_MIN_MNB_SECONDS (5 * 60)
#define MASTERNODE_PING_SECONDS (5 * 60)
#define MASTERNODE_EXPIRATION_SECONDS (65 * 60)
#define MASTERNODE_REMOVAL_SECONDS (75 * 60)
#define MASTERNODE_CHECK_SECONDS 5

using namespace std;

class CMasternode;
class CMasternodeBroadcast;
class CMasternodePing;
extern map<int64_t, uint256> mapCacheBlockHashes;

bool GetBlockHash(uint256& hash, int nBlockHeight);

//
// The Masternode Ping Class : Contains a different serialize method for sending pings from masternodes throughout the network
//

class CMasternodePing {
public:
    CTxIn vin;
    uint256 blockHash;
    int64_t sigTime; //mnb message times
    std::vector<unsigned char> vchSig;

    //Version 2 and above
    int8_t nVersion;
    std::vector<uint256> vPrevBlockHash; //10 previous blocks
    std::vector<unsigned char> vchSigPrevBlocks;

    //removed stop

    CMasternodePing();
    CMasternodePing(const CTxIn& newVin);

    SERIALIZE_METHODS(CMasternodePing, obj)
    {
        READWRITE(obj.vin);
        READWRITE(obj.blockHash);
        READWRITE(obj.sigTime);
        READWRITE(obj.vchSig);

        //New versioning is set externally before serialization
        if (obj.nVersion >= 2) {
            READWRITE(obj.nVersion);
            READWRITE(obj.vPrevBlockHash);
            READWRITE(obj.vchSigPrevBlocks);
        }
    }

    bool CheckAndUpdate(int& nDos, CConnman& connman, bool fRequireEnabled = true, bool fCheckSigTimeOnly = false) const;
    bool Sign(const CKey& keyMasternode, const CPubKey& pubKeyMasternode);
    bool VerifySignature(const CPubKey& pubKeyMasternode, int& nDos) const;
    void Relay(CConnman& connman) const;

    uint256 GetHash() const
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << vin;
        ss << sigTime;
        return ss.GetHash();
    }

    void swap(CMasternodePing& first, CMasternodePing& second) // nothrow
    {
        // enable ADL (not necessary in our case, but good practice)
        using std::swap;

        // by swapping the members of two classes,
        // the two classes are effectively swapped
        swap(first.vin, second.vin);
        swap(first.blockHash, second.blockHash);
        swap(first.sigTime, second.sigTime);
        swap(first.vchSig, second.vchSig);
    }

    CMasternodePing& operator=(CMasternodePing from)
    {
        swap(*this, from);
        return *this;
    }
    friend bool operator==(const CMasternodePing& a, const CMasternodePing& b)
    {
        return a.vin == b.vin && a.blockHash == b.blockHash;
    }
    friend bool operator!=(const CMasternodePing& a, const CMasternodePing& b)
    {
        return !(a == b);
    }
};

//
// The Masternode Class. It contains the input of the 10000 CRW, signature to prove
// it's the one who own that ip address and code for calculating the payment election.
//
class CMasternode {
private:
    // critical section to protect the inner data structures
    mutable RecursiveMutex cs;
    int64_t lastTimeChecked;

public:
    enum state {
        MASTERNODE_ENABLED = 1,
        MASTERNODE_EXPIRED = 2,
        MASTERNODE_VIN_SPENT = 3,
        MASTERNODE_REMOVE = 4,
        MASTERNODE_POS_ERROR = 5
    };

    enum CollateralStatus {
        COLLATERAL_OK,
        COLLATERAL_UTXO_NOT_FOUND,
        COLLATERAL_INVALID_AMOUNT
    };

    CTxIn vin;
    CService addr;
    CPubKey pubkey;
    CPubKey pubkey2;
    std::vector<unsigned char> sig;
    int activeState;
    int64_t sigTime; //mnb message time
    int cacheInputAge;
    int cacheInputAgeBlock;
    bool unitTest;
    bool allowFreeTx;
    int protocolVersion;
    int64_t nLastDsq; //the dsq count from the last dsq broadcast of this node
    int nScanningErrorCount;
    int nLastScanningErrorBlockHeight;
    CMasternodePing lastPing;
    std::vector<unsigned char> vchSignover;

    CMasternode();
    CMasternode(const CMasternode& other);
    CMasternode(const CMasternodeBroadcast& mnb);

    void swap(CMasternode& first, CMasternode& second) // nothrow
    {
        // enable ADL (not necessary in our case, but good practice)
        using std::swap;

        // by swapping the members of two classes,
        // the two classes are effectively swapped
        swap(first.vin, second.vin);
        swap(first.addr, second.addr);
        swap(first.pubkey, second.pubkey);
        swap(first.pubkey2, second.pubkey2);
        swap(first.sig, second.sig);
        swap(first.activeState, second.activeState);
        swap(first.sigTime, second.sigTime);
        swap(first.lastPing, second.lastPing);
        swap(first.cacheInputAge, second.cacheInputAge);
        swap(first.cacheInputAgeBlock, second.cacheInputAgeBlock);
        swap(first.unitTest, second.unitTest);
        swap(first.allowFreeTx, second.allowFreeTx);
        swap(first.protocolVersion, second.protocolVersion);
        swap(first.nLastDsq, second.nLastDsq);
        swap(first.nScanningErrorCount, second.nScanningErrorCount);
        swap(first.nLastScanningErrorBlockHeight, second.nLastScanningErrorBlockHeight);
        swap(first.vchSignover, second.vchSignover);
    }

    CMasternode& operator=(CMasternode from)
    {
        swap(*this, from);
        return *this;
    }
    friend bool operator==(const CMasternode& a, const CMasternode& b)
    {
        return a.vin == b.vin;
    }
    friend bool operator!=(const CMasternode& a, const CMasternode& b)
    {
        return !(a.vin == b.vin);
    }

    arith_uint256 CalculateScore(int64_t nBlockHeight = 0) const;

    SERIALIZE_METHODS(CMasternode, obj)
    {
        LOCK(obj.cs);

        READWRITE(obj.vin);
        READWRITE(obj.addr);
        READWRITE(obj.pubkey);
        READWRITE(obj.pubkey2);
        READWRITE(obj.sig);
        READWRITE(obj.sigTime);
        READWRITE(obj.protocolVersion);
        READWRITE(obj.activeState);
        READWRITE(obj.lastPing);
        READWRITE(obj.cacheInputAge);
        READWRITE(obj.cacheInputAgeBlock);
        READWRITE(obj.unitTest);
        READWRITE(obj.allowFreeTx);
        READWRITE(obj.nLastDsq);
        READWRITE(obj.nScanningErrorCount);
        READWRITE(obj.nLastScanningErrorBlockHeight);
        READWRITE(obj.vchSignover);
    }

    static CollateralStatus CheckCollateral(const COutPoint& outpoint);
    static CollateralStatus CheckCollateral(const COutPoint& outpoint, int& nHeightRet);

    int64_t SecondsSincePayment() const;
    bool UpdateFromNewBroadcast(const CMasternodeBroadcast& mnb, CConnman& connman);
    void Check(bool forceCheck = false);

    bool IsBroadcastedWithin(int seconds) const
    {
        return (GetAdjustedTime() - sigTime) < seconds;
    }

    bool IsPingedWithin(int seconds, int64_t now = -1) const
    {
        now == -1 ? now = GetAdjustedTime() : now;

        return (lastPing == CMasternodePing())
            ? false
            : now - lastPing.sigTime < seconds;
    }

    void Disable()
    {
        sigTime = 0;
        lastPing = CMasternodePing();
    }

    bool IsEnabled() const
    {
        return activeState == MASTERNODE_ENABLED;
    }

    bool IsValidNetAddr() const;

    int GetMasternodeInputAge()
    {
        if (::ChainActive().Tip() == nullptr)
            return 0;

        if (cacheInputAge == 0) {
            cacheInputAge = GetUTXOConfirmations(vin.prevout);
            cacheInputAgeBlock = ::ChainActive().Tip()->nHeight;
        }

        return cacheInputAge + (::ChainActive().Tip()->nHeight - cacheInputAgeBlock);
    }

    std::string Status() const
    {
        std::string strStatus = "ACTIVE";

        if (activeState == CMasternode::MASTERNODE_ENABLED)
            strStatus = "ENABLED";
        if (activeState == CMasternode::MASTERNODE_EXPIRED)
            strStatus = "EXPIRED";
        if (activeState == CMasternode::MASTERNODE_VIN_SPENT)
            strStatus = "VIN_SPENT";
        if (activeState == CMasternode::MASTERNODE_REMOVE)
            strStatus = "REMOVE";
        if (activeState == CMasternode::MASTERNODE_POS_ERROR)
            strStatus = "POS_ERROR";

        return strStatus;
    }

    int64_t GetLastPaid() const;

    bool GetRecentPaymentBlocks(std::vector<const CBlockIndex*>& vPaymentBlocks, bool limitMostRecent = false) const;
};

//
// The Masternode Broadcast Class : Contains a different serialize method for sending masternodes through the network
//

class CMasternodeBroadcast : public CMasternode {
public:
    CMasternodeBroadcast();
    CMasternodeBroadcast(CService newAddr, CTxIn newVin, CPubKey newPubkey, CPubKey newPubkey2, int protocolVersionIn);
    CMasternodeBroadcast(const CMasternode& mn);

    /// Create Masternode broadcast, needs to be relayed manually after that
    static bool Create(CTxIn txin, CService service, CKey keyCollateral, CPubKey pubKeyCollateral, CKey keyMasternodeNew, CPubKey pubKeyMasternodeNew, bool fSignOver, std::string& strErrorMessage, CMasternodeBroadcast& mnb);
    static bool Create(std::string strService, std::string strKey, std::string strTxHash, std::string strOutputIndex, std::string& strErrorMessage, CMasternodeBroadcast& mnb, bool fOffline = false);

    bool CheckAndUpdate(int& nDoS, CConnman& connman) const;
    bool CheckInputsAndAdd(int& nDos, CConnman& connman) const;
    bool Sign(const CKey& keyCollateralAddress);
    bool VerifySignature() const;
    void Relay(CConnman& connman) const;

    SERIALIZE_METHODS(CMasternodeBroadcast, obj)
    {
        READWRITE(obj.vin);
        READWRITE(obj.addr);
        READWRITE(obj.pubkey);
        READWRITE(obj.pubkey2);
        READWRITE(obj.sig);
        READWRITE(obj.sigTime);
        READWRITE(obj.protocolVersion);
        READWRITE(obj.lastPing);
        READWRITE(obj.nLastDsq);
        if (obj.protocolVersion >= PROTOCOL_POS_START) {
            READWRITE(obj.vchSignover);
        }
    }

    uint256 GetHash() const
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << sigTime;
        ss << pubkey;
        return ss.GetHash();
    }
};

#endif
