#include <crown/legacycalls.h>

#include <crown/instantx.h>
#include <util/system.h>
#include <validation.h>

class uint256;

int GetIXConfirmations(uint256 nTXHash)
{
    int sigs = instantSend.GetSignaturesCount(nTXHash);

    if (sigs >= INSTANTX_SIGNATURES_REQUIRED) {
        return nInstantXDepth;
    }

    return 0;
}

int GetTransactionAge(const uint256& txid)
{
    uint256 hashBlock;
    CTransactionRef tx = GetTransaction(::ChainActive().Tip(), nullptr, txid, Params().GetConsensus(), hashBlock);
    if (tx) {
        BlockMap::iterator it = g_chainman.BlockIndex().find(hashBlock);
        if (it != g_chainman.BlockIndex().end()) {
            return (::ChainActive().Tip()->nHeight + 1) - it->second->nHeight;
        }
    }

    return -1;
}
