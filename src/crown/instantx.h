// Copyright (c) 2009-2015 The Dash developers
// Copyright (c) 2014-2018 The Crown developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef INSTANTX_H
#define INSTANTX_H

#include <base58.h>
#include <crown/spork.h>
#include <key.h>
#include <net.h>
#include <sync.h>
#include <util/system.h>

/*
    At 15 signatures, 1/2 of the masternode network can be owned by
    one party without comprimising the security of InstantX
    (1000/2150.0)**10 = 0.00047382219560689856
    (1000/2900.0)**10 = 2.3769498616783657e-05

    ### getting 5 of 10 signatures w/ 1000 nodes of 2900
    (1000/2900.0)**5 = 0.004875397277841433
*/
#define INSTANTX_SIGNATURES_REQUIRED 6
#define INSTANTX_SIGNATURES_TOTAL 10

class CConsensusVote;
class CTransactionLock;
class InstantSend;

static const int MIN_INSTANTX_PROTO_VERSION = 70040;

extern InstantSend instantSend;

class InstantSend {
public:
    void ProcessMessage(CNode* pfrom, const std::string& strCommand, CDataStream& vRecv, CConnman* connman);
    void CheckAndRemove();
    void Clear();
    int64_t CreateNewLock(const CMutableTransaction& tx);
    int GetSignaturesCount(uint256 txHash) const;
    int GetCompleteLocksCount() const;
    bool IsLockTimedOut(uint256 txHash) const;
    bool TxLockRequested(uint256 txHash) const;
    bool AlreadyHave(uint256 txHash) const;
    std::string ToString() const;
    boost::optional<uint256> GetLockedTx(const COutPoint& out) const;
    boost::optional<CConsensusVote> GetLockVote(uint256 txHash) const;
    boost::optional<CMutableTransaction> GetLockReq(uint256 txHash) const;

    SERIALIZE_METHODS(InstantSend, obj)
    {
        READWRITE(obj.m_lockedInputs);
        READWRITE(obj.m_txLockVote);
        READWRITE(obj.m_txLockReq);
        READWRITE(obj.m_txLocks);
        READWRITE(obj.m_unknownVotes);
        READWRITE(obj.m_txLockReqRejected);
        READWRITE(obj.m_completeTxLocks);
    }

public:
    static const int m_acceptedBlockCount = 24;
    static const int m_numberOfSeconds = 60;

private:
    void DoConsensusVote(const CMutableTransaction& tx, int64_t nBlockHeight, CConnman& connman);
    bool IsIxTxValid(const CMutableTransaction& txCollateral) const;
    bool ProcessConsensusVote(CNode* pnode, const CConsensusVote& ctx, CConnman& connman);
    bool CheckForConflictingLocks(const CMutableTransaction& tx);
    int64_t GetAverageVoteTime() const;

private:
    // critical section to protect the inner data structures
    mutable RecursiveMutex cs;

    std::map<COutPoint, uint256> m_lockedInputs;
    std::map<uint256, CConsensusVote> m_txLockVote;
    std::map<uint256, CMutableTransaction> m_txLockReq;
    std::map<uint256, CTransactionLock> m_txLocks;
    std::map<uint256, int64_t> m_unknownVotes; //track votes with no tx for DOS
    std::map<uint256, CMutableTransaction> m_txLockReqRejected;
    int m_completeTxLocks;
};

class CConsensusVote {
public:
    CConsensusVote()
        : m_expiration(GetTime() + (InstantSend::m_numberOfSeconds * InstantSend::m_acceptedBlockCount))
    {
    }
    uint256 GetHash() const;
    bool SignatureValid() const;
    bool Sign();

    SERIALIZE_METHODS(CConsensusVote, obj)
    {
        READWRITE(obj.txHash);
        READWRITE(obj.vinMasternode);
        READWRITE(obj.vchMasterNodeSignature);
        READWRITE(obj.nBlockHeight);
        READWRITE(obj.m_expiration);
    }

public:
    CTxIn vinMasternode;
    uint256 txHash;
    int nBlockHeight;
    std::vector<unsigned char> vchMasterNodeSignature;
    int m_expiration;
};

class CTransactionLock {
public:
    CTransactionLock()
        : m_expiration(GetTime() + (InstantSend::m_numberOfSeconds * InstantSend::m_acceptedBlockCount))
        , m_timeout(GetTime() + (InstantSend::m_numberOfSeconds * 5))
    {
    }
    bool SignaturesValid() const;
    int CountSignatures() const;
    void AddSignature(const CConsensusVote& cv);
    uint256 GetHash() const;

    SERIALIZE_METHODS(CTransactionLock, obj)
    {
        READWRITE(obj.nBlockHeight);
        READWRITE(obj.txHash);
        READWRITE(obj.vecConsensusVotes);
        READWRITE(obj.m_expiration);
        READWRITE(obj.m_timeout);
    }

public:
    int nBlockHeight;
    uint256 txHash;
    std::vector<CConsensusVote> vecConsensusVotes;
    int m_expiration;
    int m_timeout;
};

#endif
