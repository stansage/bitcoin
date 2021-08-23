// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2014-2018 The Crown developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <crown/nodesync.h>

#include <index/txindex.h>
#include <init.h>
#include <masternode/masternodeman.h>
#include <masternode/masternode-sync.h>
#include <masternode/activemasternode.h>
#include <node/context.h>
#include <rpc/blockchain.h>
#include <rpc/util.h>
#include <script/sign.h>
#include <shutdown.h>
#include <util/message.h>
#include <util/system.h>
#include <util/time.h>
#include <wallet/rpcwallet.h>
#include <wallet/wallet.h>

#include <boost/algorithm/string/replace.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/lexical_cast.hpp>

#include <algorithm>
#include <boost/assign/list_of.hpp>

std::string currentSyncStatus()
{
    static int64_t lastStatusTime;
    static std::string lastStatusMessage;

    if (lastStatusTime != GetTime()) {
        lastStatusTime = GetTime();
        lastStatusMessage = masternodeSync.GetSyncStatus();
        return lastStatusMessage;
    }

    return lastStatusMessage;
}

void ThreadMasternodeSync(CConnman& connman)
{
    util::ThreadRename("crown-mnodesync");

    if (fReindex || fImporting)
        return;
    if (::ChainstateActive().IsInitialBlockDownload())
        return;
    if (ShutdownRequested())
        return;
    if (!masternodeSync.IsBlockchainSynced())
        return;

    static unsigned int c1 = 0;

    masternodeSync.Process(connman);
    {
        c1++;

        mnodeman.Check();

        // check if we should activate or ping every few minutes,
        // start right after sync is considered to be done
        if (c1 % MASTERNODE_PING_SECONDS == 15)
            activeMasternode.ManageStatus(connman);

        if (c1 % 60 == 0) {
            mnodeman.CheckAndRemove();
            mnodeman.ProcessMasternodeConnections(connman);
//            instantSend.CheckAndRemove();
        }
    }
}

