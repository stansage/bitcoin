// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2020 The Bitcoin Core developers
// Copyright (c) 2020 The Crown developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CROWN_NODEWALLET_H
#define CROWN_NODEWALLET_H

#include <consensus/validation.h>
#include <masternode/activemasternode.h>
#include <masternode/masternode-budget.h>
#include <masternode/masternode-sync.h>
#include <masternode/masternodeman.h>
#include <miner.h>
#include <node/context.h>
#include <rpc/blockchain.h>
#include <shutdown.h>
#include <util/translation.h>
#include <wallet/coincontrol.h>
#include <wallet/wallet.h>

void GetScriptForMining(CScript& script, std::shared_ptr<CWallet> wallet = GetMainWallet());
void NodeMinter(const CChainParams& chainparams, CConnman& connman);

#endif // CROWN_NODEWALLET_H
