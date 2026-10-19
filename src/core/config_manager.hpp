#pragma once
#include <obs-data.h>
#include "bilibili_api.hpp"

namespace Core {
class ConfigManager {
public:
	explicit ConfigManager(std::string configDirectory = {});
	bool load();
	bool save();
	Bili::Config &config() { return m_config; }
	const Bili::Config &config() const { return m_config; }

private:
	Bili::Config m_config;
	std::string m_configDirectory;
	char *getConfigPath() const;
	char *getLegacyConfigPath() const;
	bool loadSettings(obs_data_t *settings, bool legacyFormat, std::string &message);
	void sanitizeLegacyConfig(const char *legacyPath);
};
} // namespace Core
