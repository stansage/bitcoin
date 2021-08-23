#include <addrman.h>
#include <crown/spork.h>
#include <net.h>
#include <netbase.h>
#include <protocol.h>
#include <systemnode/activesystemnode.h>
#include <systemnode/systemnode.h>
#include <systemnode/systemnodeman.h>
#include <validation.h>
#include <wallet/coincontrol.h>

CActiveSystemnode activeSystemnode;

//
// Bootup the Systemnode, look for a 10000 CRW input and register on the network
//
void CActiveSystemnode::ManageStatus(CConnman& connman)
{
    std::string errorMessage;

    if (!fSystemNode)
        return;

    LogPrintf("CActiveSystemnode::ManageStatus() - Begin\n");

    //need correct blocks to send ping
    if (Params().NetworkIDString() != CBaseChainParams::REGTEST && !systemnodeSync.IsBlockchainSynced()) {
        status = ACTIVE_SYSTEMNODE_SYNC_IN_PROCESS;
        LogPrintf("CActiveSystemnode::ManageStatus() - %s\n", GetStatus());
        return;
    }

    if (status == ACTIVE_SYSTEMNODE_SYNC_IN_PROCESS)
        status = ACTIVE_SYSTEMNODE_INITIAL;

    if (status == ACTIVE_SYSTEMNODE_INITIAL) {
        CSystemnode* psn;
        psn = snodeman.Find(pubKeySystemnode);
        if (psn != nullptr) {
            psn->Check();
            if (psn->IsEnabled() && psn->protocolVersion == PROTOCOL_VERSION) {
                EnableHotColdSystemNode(psn->vin, psn->addr);
                if (!psn->vchSignover.empty()) {
                    if (psn->pubkey.Verify(pubKeySystemnode.GetHash(), psn->vchSignover)) {
                        LogPrintf("%s: Verified pubkey2 signover for staking\n", __func__);
                        activeSystemnode.vchSigSignover = psn->vchSignover;
                    } else {
                        LogPrintf("%s: Failed to verify pubkey on signover!\n", __func__);
                    }
                } else {
                    LogPrintf("%s: NOT SIGNOVER!\n", __func__);
                }
            }
        }
    }

    if (status != ACTIVE_SYSTEMNODE_STARTED) {

        // Set defaults
        status = ACTIVE_SYSTEMNODE_NOT_CAPABLE;
        notCapableReason = "";

        const auto pwallet = GetMainWallet();

        if (pwallet->IsLocked()) {
            notCapableReason = "Wallet is locked.";
            LogPrintf("CActiveSystemnode::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }

        CCoinControl coin_control;
        if (pwallet->GetBalance(0, coin_control.m_avoid_address_reuse).m_mine_trusted == 0) {
            notCapableReason = "Systemnode configured correctly and ready, please use your local wallet to start it -Start alias-.";
            LogPrintf("CActiveSystemnode::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }

        if (strSystemNodeAddr.empty()) {
            if (!GetLocal(service)) {
                notCapableReason = "Can't detect external address. Please use the systemnodeaddr configuration option.";
                LogPrintf("CActiveSystemnode::ManageStatus() - not capable: %s\n", notCapableReason);
                return;
            }
        } else {
            service = CService(strSystemNodeAddr);
        }

        if (Params().NetworkIDString() == CBaseChainParams::MAIN) {
            if (service.GetPort() != 9340) {
                notCapableReason = strprintf("Invalid port: %u - only 9340 is supported on mainnet.", service.GetPort());
                LogPrintf("CActiveSystemnode::ManageStatus() - not capable: %s\n", notCapableReason);
                return;
            }
        } else if (service.GetPort() == 9340) {
            notCapableReason = strprintf("Invalid port: %u - 9340 is only supported on mainnet.", service.GetPort());
            LogPrintf("CActiveSystemnode::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }

        LogPrintf("CActiveSystemnode::ManageStatus() - Checking inbound connection to '%s'\n", service.ToString());

        SOCKET hSocket = INVALID_SOCKET;
        hSocket = CreateSocket(service);
        if (hSocket == INVALID_SOCKET) {
            LogPrintf("CActiveSystemnode::ManageStateInitial -- Could not create socket '%s'\n", service.ToString());
            return;
        }
        bool fConnected = ConnectSocketDirectly(service, hSocket, nConnectTimeout, true) && IsSelectableSocket(hSocket);
        CloseSocket(hSocket);

        if (!fConnected) {
            notCapableReason = "Could not connect to " + service.ToString();
            LogPrintf("CActiveSystemnode::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }

        // Choose coins to use
        CPubKey pubKeyCollateralAddress;
        CKey keyCollateralAddress;

        if (pwallet->GetSystemnodeVinAndKeys(vin, pubKeyCollateralAddress, keyCollateralAddress)) {
            if (GetUTXOConfirmations(vin.prevout) < SYSTEMNODE_MIN_CONFIRMATIONS) {
                status = ACTIVE_SYSTEMNODE_INPUT_TOO_NEW;
                notCapableReason = strprintf("%s - %d confirmations", GetStatus(), GetUTXOConfirmations(vin.prevout));
                LogPrintf("CActiveSystemnode::ManageStatus() - %s\n", notCapableReason);
                return;
            }

            LOCK(pwallet->cs_wallet);
            pwallet->LockCoin(vin.prevout);

            // send to all nodes
            CPubKey pubKeySystemnode;
            CKey keySystemnode;

            if (!legacySigner.SetKey(strSystemNodePrivKey, keySystemnode, pubKeySystemnode)) {
                notCapableReason = "Error upon calling SetKey: " + errorMessage;
                LogPrintf("Register::ManageStatus() - %s\n", notCapableReason);
                return;
            }

            CSystemnodeBroadcast mnb;
            bool fSignOver = true;
            if (!CSystemnodeBroadcast::Create(vin, service, keyCollateralAddress, pubKeyCollateralAddress, keySystemnode, pubKeySystemnode, fSignOver, errorMessage, mnb)) {
                notCapableReason = "Error on CreateBroadcast: " + errorMessage;
                LogPrintf("Register::ManageStatus() - %s\n", notCapableReason);
                return;
            }

            //update to Systemnode list
            LogPrintf("CActiveSystemnode::ManageStatus() - Update Systemnode List\n");
            snodeman.UpdateSystemnodeList(mnb, connman);

            //send to all peers
            LogPrintf("CActiveSystemnode::ManageStatus() - Relay broadcast vin = %s\n", vin.ToString());
            mnb.Relay(connman);

            LogPrintf("CActiveSystemnode::ManageStatus() - Is capable Systemnode!\n");
            status = ACTIVE_SYSTEMNODE_STARTED;

            return;
        } else {
            notCapableReason = "Could not find suitable coins!";
            LogPrintf("CActiveSystemnode::ManageStatus() - %s\n", notCapableReason);
            return;
        }
    }

    //send to all peers
    if (!SendSystemnodePing(errorMessage, connman)) {
        LogPrintf("CActiveSystemnode::ManageStatus() - Error on Ping: %s\n", errorMessage);
    }
}

std::string CActiveSystemnode::GetStatus()
{
    switch (status) {
    case ACTIVE_SYSTEMNODE_INITIAL:
        return "Node just started, not yet activated";
    case ACTIVE_SYSTEMNODE_SYNC_IN_PROCESS:
        return "Sync in progress. Must wait until sync is complete to start Systemnode";
    case ACTIVE_SYSTEMNODE_INPUT_TOO_NEW:
        return strprintf("Systemnode input must have at least %d confirmations", SYSTEMNODE_MIN_CONFIRMATIONS);
    case ACTIVE_SYSTEMNODE_NOT_CAPABLE:
        return "Not capable systemnode: " + notCapableReason;
    case ACTIVE_SYSTEMNODE_STARTED:
        return "Systemnode successfully started";
    default:
        return "unknown";
    }
}

bool CActiveSystemnode::SendSystemnodePing(std::string& errorMessage, CConnman& connman)
{
    if (status != ACTIVE_SYSTEMNODE_STARTED) {
        errorMessage = "Systemnode is not in a running status";
        return false;
    }

    CPubKey pubKeySystemnode;
    CKey keySystemnode;

    if (!legacySigner.SetKey(strSystemNodePrivKey, keySystemnode, pubKeySystemnode)) {
        errorMessage = strprintf("Error upon calling SetKey: %s\n", errorMessage);
        return false;
    }

    LogPrintf("CActiveSystemnode::SendSystemnodePing() - Relay Systemnode Ping vin = %s\n", vin.ToString());

    CSystemnodePing mnp(vin);
    if (!mnp.Sign(keySystemnode, pubKeySystemnode)) {
        errorMessage = "Couldn't sign Systemnode Ping";
        return false;
    }

    // Update lastPing for our systemnode in Systemnode list
    CSystemnode* pmn = snodeman.Find(vin);
    if (pmn != nullptr) {
        if (pmn->IsPingedWithin(SYSTEMNODE_PING_SECONDS, mnp.sigTime)) {
            errorMessage = "Too early to send Systemnode Ping";
            return false;
        }

        pmn->lastPing = mnp;
        snodeman.mapSeenSystemnodePing.insert(make_pair(mnp.GetHash(), mnp));

        //snodeman.mapSeenSystemnodeBroadcast.lastPing is probably outdated, so we'll update it
        CSystemnodeBroadcast mnb(*pmn);
        uint256 hash = mnb.GetHash();
        if (snodeman.mapSeenSystemnodeBroadcast.count(hash))
            snodeman.mapSeenSystemnodeBroadcast[hash].lastPing = mnp;

        mnp.Relay(connman);

        return true;
    } else {
        // Seems like we are trying to send a ping while the Systemnode is not registered in the network
        errorMessage = "Systemnode List doesn't include our Systemnode, shutting down Systemnode pinging service! " + vin.ToString();
        status = ACTIVE_SYSTEMNODE_NOT_CAPABLE;
        notCapableReason = errorMessage;
        return false;
    }
}

// when starting a Systemnode, this can enable to run as a hot wallet with no funds
bool CActiveSystemnode::EnableHotColdSystemNode(const CTxIn& newVin, const CService& newService)
{
    if (!fSystemNode)
        return false;

    status = ACTIVE_SYSTEMNODE_STARTED;

    //The values below are needed for signing mnping messages going forward
    vin = newVin;
    service = newService;

    LogPrintf("CActiveSystemnode::EnableHotColdSystemNode() - Enabled! You may shut down the cold daemon.\n");

    return true;
}
