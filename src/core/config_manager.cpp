#include "core/config_manager.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QJsonDocument>
#include <QJsonObject>
#include <obs-module.h>

#include "core/api_utils.hpp"
#include "core/secret_store.hpp"
#include "plugin_utils.hpp"

namespace Core {
ConfigManager::ConfigManager(std::string configDirectory) : m_configDirectory(std::move(configDirectory)) {}

char *ConfigManager::getConfigPath() const
{
	if (!m_configDirectory.empty())
		return bstrdup((m_configDirectory + "/config.json").c_str());
	return obs_module_config_path("config.json");
}

char *ConfigManager::getLegacyConfigPath() const
{
	if (!m_configDirectory.empty())
		return bstrdup((m_configDirectory + "/legacy.json").c_str());
	return obs_module_file("config.json");
}

bool ConfigManager::loadSettings(obs_data_t *settings, bool legacyFormat, std::string &message)
{
	m_config.room_id = obs_data_get_string(settings, "room_id");
	m_config.mid = obs_data_get_string(settings, "mid");
	m_config.title = obs_data_get_string(settings, "title");
	obs_data_set_default_bool(settings, "sync_obs_start", true);
	obs_data_set_default_bool(settings, "sync_obs_stop", true);
	m_config.sync_obs_start = obs_data_get_bool(settings, "sync_obs_start");
	m_config.sync_obs_stop = obs_data_get_bool(settings, "sync_obs_stop");
	m_config.proxy_unlocked = obs_data_get_bool(settings, "proxy_unlocked");
	const std::string protectedStream = obs_data_get_string(settings, "stream_credentials_protected");
	if (!protectedStream.empty()) {
		std::string plain;
		if (!unprotectSecret(protectedStream, plain, message))
			return false;
		const auto credentials = QJsonDocument::fromJson(QByteArray::fromStdString(plain)).object();
		m_config.last_rtmp_address = credentials["address"].toString().toStdString();
		m_config.last_rtmp_key = credentials["key"].toString().toStdString();
		m_config.selected_rtmp_address = credentials["selected"].toString().toStdString();
	}
	m_config.managed_session = obs_data_get_bool(settings, "managed_session") ||
				   (legacyFormat && obs_data_get_bool(settings, "streaming"));
	m_config.part_id = static_cast<int>(obs_data_get_int(settings, "part_id"));
	m_config.area_id = static_cast<int>(obs_data_get_int(settings, "area_id"));

	const std::string protectedCookies = obs_data_get_string(settings, "cookies_protected");
	if (!protectedCookies.empty()) {
		if (!unprotectSecret(protectedCookies, m_config.cookies, message)) {
			return false;
		}
	} else if (legacyFormat) {
		m_config.cookies = obs_data_get_string(settings, "cookies");
	}
	m_config.csrf_token = extractCookieValue(m_config.cookies, "bili_jct");
	return true;
}

bool ConfigManager::load()
{
	m_config = Bili::Config();
	char *configFile = getConfigPath();
	if (!configFile) {
		obs_log(LOG_ERROR, "OBS module configuration directory is unavailable");
		return false;
	}

	bool legacyFormat = false;
	char *legacyFile = nullptr;
	obs_data_t *settings = obs_data_create_from_json_file(configFile);
	if (!settings) {
		legacyFile = getLegacyConfigPath();
		if (legacyFile) {
			settings = obs_data_create_from_json_file(legacyFile);
			legacyFormat = settings != nullptr;
		}
	}

	if (!settings) {
		bfree(legacyFile);
		bfree(configFile);
		return true;
	}

	const std::string storedCookies = obs_data_get_string(settings, "cookies_protected");
	const bool needsProtectionUpgrade = usesNativeSecretProtection() && storedCookies.rfind("plain-v1:", 0) == 0;
	std::string message;
	const bool loaded = loadSettings(settings, legacyFormat, message);
	obs_data_release(settings);
	if (!loaded) {
		obs_log(LOG_ERROR, "Failed to load protected plugin configuration: %s", message.c_str());
		m_config = Bili::Config();
		bfree(legacyFile);
		bfree(configFile);
		return false;
	}

	if (legacyFormat || needsProtectionUpgrade) {
		if (save()) {
			if (legacyFormat) {
				sanitizeLegacyConfig(legacyFile);
				obs_log(LOG_INFO, "Migrated plugin state to the per-user OBS configuration directory");
			} else {
				obs_log(LOG_INFO, "Upgraded stored plugin credentials to native protection");
			}
		} else {
			obs_log(LOG_ERROR, "Failed to migrate or upgrade plugin configuration");
		}
	} else if (!usesNativeSecretProtection() && !m_config.cookies.empty()) {
		obs_log(LOG_WARNING,
			"Native secret protection is unavailable; cookies use the per-user compatibility store");
	}

	bfree(legacyFile);
	bfree(configFile);
	return true;
}

bool ConfigManager::save()
{
	char *configFile = getConfigPath();
	if (!configFile) {
		obs_log(LOG_ERROR, "OBS module configuration directory is unavailable");
		return false;
	}

	const QString configPath = QString::fromUtf8(configFile);
	const QString configDirectory = QFileInfo(configPath).absolutePath();
	if (!QDir().mkpath(configDirectory)) {
		obs_log(LOG_ERROR, "Failed to create the plugin configuration directory");
		bfree(configFile);
		return false;
	}

	std::string protectedCookies;
	std::string message;
	if (!protectSecret(m_config.cookies, protectedCookies, message)) {
		obs_log(LOG_ERROR, "Failed to protect plugin credentials: %s", message.c_str());
		bfree(configFile);
		return false;
	}

	obs_data_t *settings = obs_data_create();
	std::string protectedStream;
	const QJsonObject credentials{{"address", QString::fromStdString(m_config.last_rtmp_address)},
				      {"key", QString::fromStdString(m_config.last_rtmp_key)},
				      {"selected", QString::fromStdString(m_config.selected_rtmp_address)}};
	if (!protectSecret(QJsonDocument(credentials).toJson(QJsonDocument::Compact).toStdString(), protectedStream,
			   message)) {
		obs_data_release(settings);
		bfree(configFile);
		return false;
	}
	obs_data_set_string(settings, "stream_credentials_protected", protectedStream.c_str());
	obs_data_set_bool(settings, "proxy_unlocked", m_config.proxy_unlocked);
	obs_data_set_string(settings, "room_id", m_config.room_id.c_str());
	obs_data_set_string(settings, "mid", m_config.mid.c_str());
	obs_data_set_string(settings, "title", m_config.title.c_str());
	obs_data_set_string(settings, "cookies_protected", protectedCookies.c_str());
	obs_data_set_bool(settings, "managed_session", m_config.managed_session);
	obs_data_set_bool(settings, "sync_obs_start", m_config.sync_obs_start);
	obs_data_set_bool(settings, "sync_obs_stop", m_config.sync_obs_stop);
	obs_data_set_int(settings, "part_id", m_config.part_id);
	obs_data_set_int(settings, "area_id", m_config.area_id);
	const bool saved = obs_data_save_json_safe(settings, configFile, "tmp", "bak");
	obs_data_release(settings);

	if (saved) {
		QFile::setPermissions(configPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
	} else {
		obs_log(LOG_ERROR, "Failed to save plugin configuration");
	}
	bfree(configFile);
	return saved;
}

void ConfigManager::sanitizeLegacyConfig(const char *legacyPath)
{
	if (!legacyPath)
		return;
	obs_data_t *settings = obs_data_create_from_json_file(legacyPath);
	if (!settings)
		return;
	obs_data_set_string(settings, "cookies", "");
	obs_data_set_string(settings, "csrf_token", "");
	obs_data_set_string(settings, "rtmp_addr", "");
	obs_data_set_string(settings, "rtmp_code", "");
	if (!obs_data_save_json_safe(settings, legacyPath, "tmp", "bak")) {
		obs_log(LOG_WARNING,
			"Could not redact the legacy plugin data file; remove it manually if it contains credentials");
	}
	obs_data_release(settings);
}
} // namespace Core
