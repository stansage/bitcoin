// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2014-2018 The Crown developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <crown/nodewallet.h>
#include <crown/spork.h>
#include <netbase.h>

class CActiveMasternode;
CActiveMasternode activeMasternode;

//
// Bootup the Masternode, look for a 10000 CRW input and register on the network
//
void CActiveMasternode::ManageStatus(CConnman& connman)
{
    std::string errorMessage;

    if (!fMasterNode)
        return;

    LogPrintf("CActiveMasternode::ManageStatus() - Begin\n");

    //need correct blocks to send ping
    if (!masternodeSync.IsBlockchainSynced() || !masternodeSync.IsSynced()) {
        status = ACTIVE_MASTERNODE_SYNC_IN_PROCESS;
        LogPrintf("CActiveMasternode::ManageStatus() - %s\n", GetStatus());
        return;
    }

    if (status == ACTIVE_MASTERNODE_SYNC_IN_PROCESS)
        status = ACTIVE_MASTERNODE_INITIAL;

    if (status == ACTIVE_MASTERNODE_INITIAL) {
        CMasternode* pmn;
        pmn = mnodeman.Find(pubKeyMasternode);
        if (pmn) {
            pmn->Check();
            if (pmn->IsEnabled() && pmn->protocolVersion == PROTOCOL_VERSION) {
                EnableHotColdMasterNode(pmn->vin, pmn->addr);
                if (!pmn->vchSignover.empty()) {
                    if (pmn->pubkey.Verify(pubKeyMasternode.GetHash(), pmn->vchSignover)) {
                        LogPrintf("%s: Verified pubkey2 signover for staking\n", __func__);
                        activeMasternode.vchSigSignover = pmn->vchSignover;
                    } else {
                        LogPrintf("%s: Failed to verify pubkey on signover!\n", __func__);
                    }
                } else {
                    LogPrintf("%s: NOT SIGNOVER!\n", __func__);
                }
            }
        }
    }

    if (status != ACTIVE_MASTERNODE_STARTED) {

        // Set defaults
        status = ACTIVE_MASTERNODE_NOT_CAPABLE;
        notCapableReason = "";

        const auto pwallet = GetMainWallet();

        if (pwallet && pwallet->IsLocked()) {
            notCapableReason = "Wallet is locked.";
            LogPrintf("CActiveMasternode::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }

        CCoinControl coin_control;
        if (!pwallet || (pwallet && pwallet->GetBalance(0, coin_control.m_avoid_address_reuse).m_mine_trusted == 0)) {
            notCapableReason = "Masternode configured correctly and ready, please use your local wallet to start it.";
            LogPrintf("CActiveMasternode::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }

        if (strMasterNodeAddr.empty()) {
            if (!GetLocal(service)) {
                notCapableReason = "Can't detect external address. Please use the masternodeaddr configuration option.";
                LogPrintf("CActiveMasternode::ManageStatus() - not capable: %s\n", notCapableReason);
                return;
            }
        } else {
            service = CService(strMasterNodeAddr);
        }

        if (Params().NetworkIDString() == CBaseChainParams::MAIN) {
            if (service.GetPort() != Params().GetDefaultPort()) {
                notCapableReason = strprintf("Invalid port: %u - only %u is supported on mainnet.", service.GetPort(), Params().GetDefaultPort());
                LogPrintf("CActiveMasternode::ManageStatus() - not capable: %s\n", notCapableReason);
                return;
            }
        } else if (service.GetPort() == Params().GetDefaultPort()) {
            notCapableReason = strprintf("Invalid port: %u - %u is only supported on mainnet.", service.GetPort(), Params().GetDefaultPort());
            LogPrintf("CActiveMasternode::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }

        LogPrintf("CActiveMasternode::ManageStatus() - Checking inbound connection to '%s'\n", service.ToString());

        SOCKET hSocket = INVALID_SOCKET;
        hSocket = CreateSocket(service);
        if (hSocket == INVALID_SOCKET) {
            LogPrintf("CActiveMasternode::ManageStateInitial -- Could not create socket '%s'\n", service.ToString());
            return;
        }
        bool fConnected = ConnectSocketDirectly(service, hSocket, nConnectTimeout, true) && IsSelectableSocket(hSocket);
        CloseSocket(hSocket);

        if (!fConnected) {
            notCapableReason = "Could not connect to " + service.ToString();
            LogPrintf("CActiveMasternode::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }

        // Choose coins to use
        CPubKey pubKeyCollateralAddress;
        CKey keyCollateralAddress;

        if (GetMainWallet()->GetMasternodeVinAndKeys(vin, pubKeyCollateralAddress, keyCollateralAddress)) {
            if (GetUTXOConfirmations(vin.prevout) < MASTERNODE_MIN_CONFIRMATIONS) {
                status = ACTIVE_MASTERNODE_INPUT_TOO_NEW;
                notCapableReason = strprintf("%s - %d confirmations", GetStatus(), GetUTXOConfirmations(vin.prevout));
                LogPrintf("CActiveMasternode::ManageStatus() - %s\n", notCapableReason);
                return;
            }

            LOCK(pwallet->cs_wallet);
            pwallet->LockCoin(vin.prevout);

            // send to all nodes
            CPubKey pubKeyMasternode;
            CKey keyMasternode;

            if (!legacySigner.SetKey(strMasterNodePrivKey, keyMasternode, pubKeyMasternode)) {
                notCapableReason = "Error upon calling SetKey: " + errorMessage;
                LogPrintf("Register::ManageStatus() - %s\n", notCapableReason);
                return;
            }

            CMasternodeBroadcast mnb;
            bool fSignOver = true;
            if (!CMasternodeBroadcast::Create(vin, service, keyCollateralAddress, pubKeyCollateralAddress, keyMasternode, pubKeyMasternode, fSignOver, errorMessage, mnb)) {
                notCapableReason = "Error on CreateBroadcast: " + errorMessage;
                LogPrintf("Register::ManageStatus() - %s\n", notCapableReason);
                return;
            }

            //update to masternode list
            LogPrintf("CActiveMasternode::ManageStatus() - Update Masternode List\n");
            mnodeman.UpdateMasternodeList(mnb, connman);

            //send to all peers
            LogPrintf("CActiveMasternode::ManageStatus() - Relay broadcast vin = %s\n", vin.ToString());
            mnb.Relay(connman);

            LogPrintf("CActiveMasternode::ManageStatus() - Is capable master node!\n");
            status = ACTIVE_MASTERNODE_STARTED;

            return;
        } else {
            notCapableReason = "Could not find suitable coins!";
            LogPrintf("CActiveMasternode::ManageStatus() - %s\n", notCapableReason);
            return;
        }
    }

    //send to all peers
    if (!SendMasternodePing(errorMessage, connman)) {
        LogPrintf("CActiveMasternode::ManageStatus() - Error on Ping: %s\n", errorMessage);
    }
}

std::string CActiveMasternode::GetStatus() const
{
    switch (status) {
    case ACTIVE_MASTERNODE_INITIAL:
        return "Node just started, not yet activated";
    case ACTIVE_MASTERNODE_SYNC_IN_PROCESS:
        return "Sync in progress. Must wait until sync is complete to start Masternode";
    case ACTIVE_MASTERNODE_INPUT_TOO_NEW:
        return strprintf("Masternode input must have at least %d confirmations", MASTERNODE_MIN_CONFIRMATIONS);
    case ACTIVE_MASTERNODE_NOT_CAPABLE:
        return "Not capable masternode: " + notCapableReason;
    case ACTIVE_MASTERNODE_STARTED:
        return "Masternode successfully started";
    default:
        return "unknown";
    }
}

bool CActiveMasternode::SendMasternodePing(std::string& errorMessage, CConnman& connman)
{
    if (status != ACTIVE_MASTERNODE_STARTED) {
        errorMessage = "Masternode is not in a running status";
        return false;
    }

    CPubKey pubKeyMasternode;
    CKey keyMasternode;

    if (!legacySigner.SetKey(strMasterNodePrivKey, keyMasternode, pubKeyMasternode)) {
        errorMessage = strprintf("Error upon calling SetKey: %s\n", errorMessage);
        return false;
    }

    LogPrintf("CActiveMasternode::SendMasternodePing() - Relay Masternode Ping vin = %s\n", vin.ToString());

    CMasternodePing mnp(vin);
    if (!mnp.Sign(keyMasternode, pubKeyMasternode)) {
        errorMessage = "Couldn't sign Masternode Ping";
        return false;
    }

    // Update lastPing for our masternode in Masternode list
    CMasternode* pmn = mnodeman.Find(vin);
    if (pmn) {
        if (pmn->IsPingedWithin(MASTERNODE_PING_SECONDS, mnp.sigTime)) {
            errorMessage = "Too early to send Masternode Ping";
            return false;
        }

        pmn->lastPing = mnp;
        mnodeman.mapSeenMasternodePing.insert(make_pair(mnp.GetHash(), mnp));

        //mnodeman.mapSeenMasternodeBroadcast.lastPing is probably outdated, so we'll update it
        CMasternodeBroadcast mnb(*pmn);
        uint256 hash = mnb.GetHash();
        if (mnodeman.mapSeenMasternodeBroadcast.count(hash))
            mnodeman.mapSeenMasternodeBroadcast[hash].lastPing = mnp;

        mnp.Relay(connman);

        return true;
    } else {
        // Seems like we are trying to send a ping while the Masternode is not registered in the network
        errorMessage = "Masternode List doesn't include our Masternode, shutting down Masternode pinging service! " + vin.ToString();
        status = ACTIVE_MASTERNODE_NOT_CAPABLE;
        notCapableReason = errorMessage;
        return false;
    }
}

// when starting a Masternode, this can enable to run as a hot wallet with no funds
bool CActiveMasternode::EnableHotColdMasterNode(const CTxIn& newVin, const CService& newService)
{
    if (!fMasterNode)
        return false;

    status = ACTIVE_MASTERNODE_STARTED;

    //The values below are needed for signing mnping messages going forward
    vin = newVin;
    service = newService;

    LogPrintf("CActiveMasternode::EnableHotColdMasterNode() - Enabled! You may shut down the cold daemon.\n");

    return true;
}

// get all possible outputs for running Masternode
std::vector<COutput> CActiveMasternode::SelectCoinsMasternode()
{
    std::vector<COutput> vCoins;
    std::vector<COutput> filteredCoins;

    auto m_wallet = GetMainWallet();
    if (!m_wallet)
        return filteredCoins;

    // Retrieve all possible outputs
    m_wallet->AvailableCoins(vCoins);

    // Filter appropriate coins
    for (const COutput& out : vCoins) {
        if (out.tx->tx->vout[out.i].nValue == Params().GetConsensus().nMasternodeCollateral) {
            filteredCoins.push_back(out);
        }
    }

    return filteredCoins;
}
