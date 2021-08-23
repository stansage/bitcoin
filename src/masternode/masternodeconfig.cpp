// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2014-2018 The Crown developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <masternode/masternodeconfig.h>
#include <util/system.h>

CMasternodeConfig masternodeConfig;

fs::path CMasternodeConfig::getNodeConfigFile()
{
    return GetMasternodeConfigFile();
}

std::string CMasternodeConfig::getHeader()
{
    std::string port = std::to_string(Params().GetDefaultPort());
    std::string strHeader = "# Masternode config file\n"
                            "# Format: alias IP:port masternodeprivkey collateral_output_txid collateral_output_index\n"
                            "# Example: mn1 127.0.0.2:"
        + port + " 93HaYBVUCYjEMeeH1Y4sBGLALQZE1Yc1K64xiqgX37tGBDQL8Xg "
                 "2bcd3c84c84f87eaa86e4e56834c92927a07f9e18718810b92e0d0324456a67c 0\n";
    return strHeader;
}

std::string CMasternodeConfig::getFileName()
{
    return "masternode.conf";
}
