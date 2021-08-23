#ifndef SRC_MASTERNODECONFIG_H_
#define SRC_MASTERNODECONFIG_H_

#include <fs.h>

#include <string>
#include <vector>
#include <nodeconfig.h>

class CMasternodeConfig;
extern CMasternodeConfig masternodeConfig;

class CMasternodeConfig : public CNodeConfig
{
public:
    fs::path getNodeConfigFile() override;
    std::string getHeader() override;
    std::string getFileName() override;
};

#endif /* SRC_MASTERNODECONFIG_H_ */
