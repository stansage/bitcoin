// Copyright (c) 2014-2020 The Crown developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <crown/cache.h>

void DumpCaches()
{
    CFlatDB<CMasternodeMan> flatdb1("mncache.dat", "magicMasternodeCache");
    flatdb1.Dump(mnodeman);
//    CFlatDB<CBudgetManager> flatdb5("budget.dat", "magicBudgetCache");
//    flatdb5.Dump(budget);
    CFlatDB<CNetFulfilledRequestManager> flatdb6("netfulfilled.dat", "magicFulfilledCache");
    flatdb6.Dump(netfulfilledman);
}

bool LoadCaches()
{
    std::string strDBName;
    boost::filesystem::path pathDB = GetDataDir();

    strDBName = "mncache.dat";
    uiInterface.InitMessage("Loading masternode cache...");
    CFlatDB<CMasternodeMan> flatdb1(strDBName, "magicMasternodeCache");
    if (!flatdb1.Load(mnodeman)) {
        LogPrintf("%s - Failed to load masternode cache.");
        return false;
    }

//    strDBName = "budget.dat";
//    uiInterface.InitMessage("Loading budget cache...");
//    CFlatDB<CBudgetManager> flatdb5(strDBName, "magicBudgetCache");
//    if (!flatdb5.Load(budget)) {
//        LogPrintf("%s - Failed to load budget cache.");
//        return false;
//    }

    strDBName = "netfulfilled.dat";
    uiInterface.InitMessage("Loading fulfilled requests cache...");
    CFlatDB<CNetFulfilledRequestManager> flatdb6(strDBName, "magicFulfilledCache");
    if (!flatdb6.Load(netfulfilledman)) {
        LogPrintf("%s - Failed to load fulfilled requests cache.");
        return false;
    }

    return true;
}
