// Copyright (c) 2020 The Crown developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <node/context.h>
#include <rpc/blockchain.h>
#include <primitives/transaction.h>
#include <validation.h>
#include <chainparams.h>

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

int GetInputAge(const CTxIn& vin)
{
    int height = ::ChainActive().Tip()->nHeight + 1;

    CCoinsView viewDummy;
    CCoinsViewCache view(&viewDummy);
    {
        const CTxMemPool& mempool = *g_rpc_node->mempool;
        LOCK(cs_main);
        LOCK(mempool.cs);
        CCoinsViewCache& viewChain = ::ChainstateActive().CoinsTip();
        CCoinsViewMemPool viewMempool(&viewChain, mempool);
        view.SetBackend(viewMempool); // temporarily switch cache backend to db+mempool view

        COutPoint testInput(vin.prevout.hash, vin.prevout.n);
        const Coin& coin = view.AccessCoin(testInput);

        if (!coin.IsSpent()) {
            if (coin.nHeight < 0)
                return 0;
            return height - coin.nHeight;
        } else {
            return -1;
        }
    }
}

int GetInputHeight(const CTxIn& vin)
{
    CCoinsView viewDummy;
    CCoinsViewCache view(&viewDummy);
    {
        const CTxMemPool& mempool = *g_rpc_node->mempool;
        LOCK(cs_main);
        LOCK(mempool.cs);
        CCoinsViewCache& viewChain = ::ChainstateActive().CoinsTip();
        CCoinsViewMemPool viewMempool(&viewChain, mempool);
        view.SetBackend(viewMempool); // temporarily switch cache backend to db+mempool view

        COutPoint testInput(vin.prevout.hash, vin.prevout.n);
        const Coin& coin = view.AccessCoin(testInput);

        if (!coin.IsSpent()) {
            return coin.nHeight;
        } else {
            return -1;
        }
    }
}
