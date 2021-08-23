#include <mn-pos/kernel.h>
#include <mn-pos/stakevalidation.h>

#include <arith_uint256.h>
#include <chain.h>
#include <chainparams.h>
#include <primitives/block.h>
#include <pubkey.h>
#include <util/system.h>
#include <wallet/wallet.h>

bool CheckBlockSignature(const CBlock& block, const CPubKey& pubkeyMasternode)
{
    uint256 hashBlock = block.GetHash();

    return pubkeyMasternode.Verify(hashBlock, block.vchBlockSig);
}

// Check kernel hash target and coinstake signature
bool CheckProofOfStake(const CBlock& block, const CBlockIndex* prevBlock, const COutPoint& outpointStakePointer, uint256& hashProofOfStake)
{
    const CTransactionRef tx = block.vtx[1];
    if (!tx->IsCoinStake())
        return error("CheckProofOfStake() : called on non-coinstake %s", tx->GetHash().ToString().c_str());

    // Get the stake modifier
    auto pindexModifier = prevBlock->GetAncestor(prevBlock->nHeight - Params().GetConsensus().KernelModifierOffset());
    if (!pindexModifier)
        return error("CheckProofOfStake() : could not find modifier index for stake");
    uint256 nStakeModifier = pindexModifier->GetBlockHash();

    // Get the correct amount for the collateral
    CAmount nAmountCollateral = 0;
    if (outpointStakePointer.n == 1)
        nAmountCollateral = Params().GetConsensus().nMasternodeCollateral;
    else if (outpointStakePointer.n == 2)
        nAmountCollateral = Params().GetConsensus().nSystemnodeCollateral;
    else
        return error("%s: Stake pointer is neither pos 1 or 2", __func__);

    // Reconstruct the kernel that created the stake
    auto pairOut = std::make_pair(outpointStakePointer.hash, outpointStakePointer.n);
    Kernel kernel(pairOut, nAmountCollateral, nStakeModifier, prevBlock->GetBlockTime(), block.nTime);

    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(block.nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow)
        return error("CheckProofOfStake() : nBits below minimum stake");

    LogPrintf("%s : %s\n", __func__, kernel.ToString());

    hashProofOfStake = kernel.GetStakeHash();

    return kernel.IsValidProof(ArithToUint256(bnTarget));
}
