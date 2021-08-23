// Copyright (c) 2014-2018 The Crown developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SYSTEMNODE_H
#define SYSTEMNODE_H

#include <arith_uint256.h>
#include <base58.h>
#include <key.h>
#include <net.h>
#include <sync.h>
#include <timedata.h>
#include <util/system.h>
#include <validation.h>

#define SYSTEMNODE_MIN_CONFIRMATIONS 15
#define SYSTEMNODE_MIN_SNP_SECONDS (10 * 60)
#define SYSTEMNODE_MIN_SNB_SECONDS (5 * 60)
#define SYSTEMNODE_PING_SECONDS (5 * 60)
#define SYSTEMNODE_EXPIRATION_SECONDS (65 * 60)
#define SYSTEMNODE_REMOVAL_SECONDS (75 * 60)
#define SYSTEMNODE_CHECK_SECONDS 5

using namespace std;

class CSystemnode;
class CSystemnodeBroadcast;
class CSystemnodePing;
extern map<int64_t, uint256> mapCacheBlockHashes;

//
// The Systemnode Ping Class : Contains a different serialize method for sending pings from systemnodes throughout the network
//

class CSystemnodePing {
public:
    CTxIn vin;
    uint256 blockHash;
    int64_t sigTime; //snb message times
    std::vector<unsigned char> vchSig;

    CSystemnodePing();
    CSystemnodePing(const CTxIn& newVin);

    SERIALIZE_METHODS(CSystemnodePing, obj)
    {
        READWRITE(obj.vin);
        READWRITE(obj.blockHash);
        READWRITE(obj.sigTime);
        READWRITE(obj.vchSig);
    }

    bool CheckAndUpdate(int& nDos, CConnman& connman, bool fRequireEnabled = true, bool fCheckSigTimeOnly = false);
    bool Sign(const CKey& keySystemnode, const CPubKey& pubKeySystemnode);
    bool VerifySignature(const CPubKey& pubKeySystemnode, int& nDos) const;
    void Relay(CConnman& connman);

    uint256 GetHash() const
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << vin;
        ss << sigTime;
        return ss.GetHash();
    }

    void swap(CSystemnodePing& first, CSystemnodePing& second) // nothrow
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

    CSystemnodePing& operator=(CSystemnodePing from)
    {
        swap(*this, from);
        return *this;
    }
    friend bool operator==(const CSystemnodePing& a, const CSystemnodePing& b)
    {
        return a.vin == b.vin && a.blockHash == b.blockHash;
    }
    friend bool operator!=(const CSystemnodePing& a, const CSystemnodePing& b)
    {
        return !(a == b);
    }
};

//
// The Systemnode Class. For managing the Darksend process. It contains the input of the 10000 CRW, signature to prove
// it's the one who own that ip address and code for calculating the payment election.
//
class CSystemnode {
private:
    // critical section to protect the inner data structures
    mutable RecursiveMutex cs;
    int64_t lastTimeChecked;

public:
    enum state {
        SYSTEMNODE_ENABLED = 1,
        SYSTEMNODE_EXPIRED = 2,
        SYSTEMNODE_VIN_SPENT = 3,
        SYSTEMNODE_REMOVE = 4,
        SYSTEMNODE_POS_ERROR = 5
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
    int64_t sigTime; //snb message time
    int cacheInputAge;
    int cacheInputAgeBlock;
    bool unitTest;
    int protocolVersion;
    CSystemnodePing lastPing;
    std::vector<unsigned char> vchSignover;

    CSystemnode();
    CSystemnode(const CSystemnode& other);
    CSystemnode(const CSystemnodeBroadcast& snb);

    void swap(CSystemnode& first, CSystemnode& second) // nothrow
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
        swap(first.unitTest, second.unitTest);
        swap(first.protocolVersion, second.protocolVersion);
        swap(first.vchSignover, second.vchSignover);
    }

    CSystemnode& operator=(CSystemnode from)
    {
        swap(*this, from);
        return *this;
    }
    friend bool operator==(const CSystemnode& a, const CSystemnode& b)
    {
        return a.vin == b.vin;
    }
    friend bool operator!=(const CSystemnode& a, const CSystemnode& b)
    {
        return !(a.vin == b.vin);
    }

    arith_uint256 CalculateScore(int64_t nBlockHeight = 0) const;

    SERIALIZE_METHODS(CSystemnode, obj)
    {
        READWRITE(obj.vin);
        READWRITE(obj.addr);
        READWRITE(obj.pubkey);
        READWRITE(obj.pubkey2);
        READWRITE(obj.sig);
        READWRITE(obj.sigTime);
        READWRITE(obj.protocolVersion);
        READWRITE(obj.activeState);
        READWRITE(obj.lastPing);
        READWRITE(obj.unitTest);
        READWRITE(obj.vchSignover);
    }

    static CollateralStatus CheckCollateral(const COutPoint& outpoint);
    static CollateralStatus CheckCollateral(const COutPoint& outpoint, int& nHeightRet);

    int64_t SecondsSincePayment() const;
    bool UpdateFromNewBroadcast(CSystemnodeBroadcast& snb, CConnman& connman);
    void Check(bool forceCheck = false);
    bool IsBroadcastedWithin(int seconds) const
    {
        return (GetAdjustedTime() - sigTime) < seconds;
    }
    bool IsEnabled() const
    {
        return activeState == SYSTEMNODE_ENABLED;
    }
    bool IsValidNetAddr();
    int GetSystemnodeInputAge()
    {
        if (::ChainActive().Tip() == nullptr)
            return 0;

        if (cacheInputAge == 0) {
            cacheInputAge = GetUTXOConfirmations(vin.prevout);
            cacheInputAgeBlock = ::ChainActive().Tip()->nHeight;
        }

        return cacheInputAge + (::ChainActive().Tip()->nHeight - cacheInputAgeBlock);
    }
    bool IsPingedWithin(int seconds, int64_t now = -1) const
    {
        now == -1 ? now = GetAdjustedTime() : now;

        return (lastPing == CSystemnodePing())
            ? false
            : now - lastPing.sigTime < seconds;
    }
    std::string Status() const
    {
        std::string strStatus = "ACTIVE";

        if (activeState == CSystemnode::SYSTEMNODE_ENABLED)
            strStatus = "ENABLED";
        if (activeState == CSystemnode::SYSTEMNODE_EXPIRED)
            strStatus = "EXPIRED";
        if (activeState == CSystemnode::SYSTEMNODE_VIN_SPENT)
            strStatus = "VIN_SPENT";
        if (activeState == CSystemnode::SYSTEMNODE_REMOVE)
            strStatus = "REMOVE";
        if (activeState == CSystemnode::SYSTEMNODE_POS_ERROR)
            strStatus = "POS_ERROR";

        return strStatus;
    }
    int64_t GetLastPaid() const;

    bool GetRecentPaymentBlocks(std::vector<const CBlockIndex*>& vPaymentBlocks, bool limitMostRecent = false) const;
};

//
// The Systemnode Broadcast Class : Contains a different serialize method for sending systemnodes through the network
//

class CSystemnodeBroadcast : public CSystemnode {
public:
    CSystemnodeBroadcast();
    CSystemnodeBroadcast(CService newAddr, CTxIn newVin, CPubKey newPubkey, CPubKey newPubkey2, int protocolVersionIn);
    CSystemnodeBroadcast(const CSystemnode& sn);

    /// Create Systemnode broadcast, needs to be relayed manually after that
    static bool Create(CTxIn txin, CService service, CKey keyCollateral, CPubKey pubKeyCollateral, CKey keySystemnodeNew, CPubKey pubKeySystemnodeNew, bool fSignOver, std::string& strErrorMessage, CSystemnodeBroadcast& snb);
    static bool Create(std::string strService, std::string strKey, std::string strTxHash, std::string strOutputIndex, std::string& strErrorMessage, CSystemnodeBroadcast& snb, bool fOffline = false);

    bool CheckAndUpdate(int& nDoS, CConnman& connman);
    bool CheckInputsAndAdd(int& nDos, CConnman& connman);
    bool Sign(const CKey& keyCollateralAddress);
    bool VerifySignature() const;
    void Relay(CConnman& connman) const;

    SERIALIZE_METHODS(CSystemnodeBroadcast, obj)
    {
        READWRITE(obj.vin);
        READWRITE(obj.addr);
        READWRITE(obj.pubkey);
        READWRITE(obj.pubkey2);
        READWRITE(obj.sig);
        READWRITE(obj.sigTime);
        READWRITE(obj.protocolVersion);
        READWRITE(obj.lastPing);
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
