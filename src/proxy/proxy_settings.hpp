#pragma once
#include <QString>
#include <QJsonObject>

namespace Proxy {
struct Settings {
	bool enabled = false;
	bool api = true;
	bool stream = false;
	QString uri;
	QString backend = "ss";
	QString ovpnProfile, ovpnUsername, ovpnPassword;
	bool keepConnected = false;
	bool apiEnabled() const { return enabled && api; }
	bool streamEnabled() const { return enabled && stream; }
	bool onDemandApi() const { return apiEnabled() && !stream && !keepConnected; }
	bool keepRunning() const { return enabled && (stream || keepConnected); }
};
struct Node {
	QString host, method, password, name;
	quint16 port = 0;
};
bool parseUri(const QString &uri, Node &node, QString &error);
bool parseOpenVpn(const QString &profile, QJsonObject &endpoint, bool &requiresPassword, QString &error);
bool validateNode(const Settings &settings, QString &error);
bool validate(const Settings &settings, QString &error);
bool validateChange(const Settings &current, const Settings &candidate, bool outputActive, bool operationActive,
		    bool managedSession, QString &error);
bool saveSettings(const QString &path, const Settings &settings, QString &error);
bool loadSettings(const QString &path, Settings &settings, QString &error);
QJsonObject coreConfig(const Node &node, quint16 port, const QString &user, const QString &password);
QJsonObject coreConfig(const Settings &settings, quint16 port, const QString &user, const QString &password,
		       QString &error);
} // namespace Proxy
