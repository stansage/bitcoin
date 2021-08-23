// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2014-2018 The Crown developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <base58.h>
#include <net.h>
#include <netbase.h>
#include <node/ui_interface.h>
#include <nodeconfig.h>
#include <util/system.h>
#include <util/translation.h>

void CNodeConfig::add(std::string alias, std::string ip, std::string privKey, std::string txHash, std::string outputIndex)
{
    CNodeEntry cme(alias, ip, privKey, txHash, outputIndex);
    entries.push_back(cme);
}

void CNodeConfig::add(CNodeEntry cne)
{
    entries.push_back(cne);
}

bool CNodeConfig::read(std::string& strErr)
{
    int linenumber = 1;
    const fs::path& pathNodeConfigFile = getNodeConfigFile();
    boost::filesystem::ifstream streamConfig(pathNodeConfigFile);

    if (!streamConfig.good()) {
        FILE* configFile = fopen(pathNodeConfigFile.string().c_str(), "a");
        if (configFile != NULL) {
            std::string strHeader = "# Crown node config file\n"
                                    "# Format: name IP:port nodeprivkey collateral_output_txid collateral_output_index\n"
                                    "# Example: alias0 127.0.0.2:8369 93HaYBVUCYjEMeeH1Y4sBGLALQZE1Yc1K64xiqgX37tGBDQL8Xg 2bcd3c84c84f87eaa86e4e56834c92927a07f9e18718810b92e0d0324456a67c 0\n";
            fwrite(strHeader.c_str(), std::strlen(strHeader.c_str()), 1, configFile);
            fclose(configFile);
        }
        return true; // Nothing to read, so just return
    }

    for (std::string line; std::getline(streamConfig, line); linenumber++) {
        if (line.empty())
            continue;

        std::istringstream iss(line);
        std::string comment, alias, ip, privKey, txHash, outputIndex;

        if (iss >> comment) {
            if (comment.at(0) == '#')
                continue;
            iss.str(line);
            iss.clear();
        }

        if (!(iss >> alias >> ip >> privKey >> txHash >> outputIndex)) {
            iss.str(line);
            iss.clear();
            if (!(iss >> alias >> ip >> privKey >> txHash >> outputIndex)) {
                strErr = _("Could not parse masternode.conf").translated + "\n" + strprintf(_("Line: %d").translated, linenumber) + "\n\"" + line + "\"";
                streamConfig.close();
                return false;
            }
        }

        int port = 0;
        std::string hostname = "";
        SplitHostPort(ip, port, hostname);
        if (port == 0 || hostname == "") {
            strErr = _("Failed to parse host:port string").translated + "\n" + strprintf(_("Line: %d").translated, linenumber) + "\n\"" + line + "\"";
            streamConfig.close();
            return false;
        }

        add(alias, ip, privKey, txHash, outputIndex);
    }

    streamConfig.close();
    return true;
}

bool CNodeConfig::aliasExists(const std::string& alias)
{
    for (CNodeEntry mne : getEntries()) {
        if (mne.getAlias() == alias) {
            return true;
        }
    }
    return false;
}

void CNodeConfig::clear()
{
    entries.clear();
}

bool CNodeConfig::write()
{
    fs::path pathNodeConfigFile = getNodeConfigFile();
    boost::filesystem::ofstream streamConfig(pathNodeConfigFile, std::ofstream::out);
    streamConfig << getHeader() << "\n";
    for (CNodeEntry sne : getEntries()) {
        streamConfig << sne.getAlias() << " "
                     << sne.getIp() << " "
                     << sne.getPrivKey() << " "
                     << sne.getTxHash() << " "
                     << sne.getOutputIndex() << "\n";
    }
    streamConfig.close();
    return true;
}
