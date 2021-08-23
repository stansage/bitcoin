// Copyright (c) 2014-2018 The Crown developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <base58.h>
#include <net.h>
#include <node/ui_interface.h>
#include <systemnode/systemnodeconfig.h>
#include <util/system.h>

CSystemnodeConfig systemnodeConfig;

boost::filesystem::path CSystemnodeConfig::getNodeConfigFile()
{
    return GetSystemnodeConfigFile();
}

std::string CSystemnodeConfig::getHeader()
{
    std::string port = "9340";
    if (Params().NetworkIDString() == CBaseChainParams::TESTNET) {
        port = "19340";
    }
    std::string strHeader = "# Systemnode config file\n"
                            "# Format: alias IP:port systemnodeprivkey collateral_output_txid collateral_output_index\n"
                            "# Example: mn1 127.0.0.2:"
        + port + " 93HaYBVUCYjEMeeH1Y4sBGLALQZE1Yc1K64xiqgX37tGBDQL8Xg "
                 "2bcd3c84c84f87eaa86e4e56834c92927a07f9e18718810b92e0d0324456a67c 0\n";
    return strHeader;
}

std::string CSystemnodeConfig::getFileName()
{
    return "systemnode.conf";
}
