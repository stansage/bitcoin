// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2014-2018 The Crown developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SRC_MASTERNODECONFIG_H_
#define SRC_MASTERNODECONFIG_H_

#include <string>
#include <vector>

#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/foreach.hpp>

class CMasternodeConfig;
extern CMasternodeConfig masternodeConfig;

class CMasternodeConfig {
private:
    boost::filesystem::path getNodeConfigFile();
    std::string getHeader();
    std::string getFileName();
};

#endif /* SRC_MASTERNODECONFIG_H_ */
