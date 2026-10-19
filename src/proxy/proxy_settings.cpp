#include "proxy_settings.hpp"
#include "core/secret_store.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QUrl>
#include <QUrlQuery>

namespace Proxy {
bool parseUri(const QString &input, Node &node, QString &error)
{
	node = {};
	error = QStringLiteral("SS 链接格式无效，请检查加密方式、服务器和端口。");
	const QString uri = input.trimmed();
	if (uri.size() > 8192 || !uri.startsWith("ss://"))
		return false;
	for (const QChar c : uri)
		if (c.isSpace() || c.unicode() < 32 || c.unicode() == 127)
			return false;
	QString body = uri.mid(5);
	const int fragment = body.indexOf('#');
	if (fragment >= 0) {
		node.name = QUrl::fromPercentEncoding(body.mid(fragment + 1).toUtf8());
		body.truncate(fragment);
	}
	const int queryIndex = body.indexOf('?');
	if (queryIndex >= 0) {
		const QUrlQuery query(body.mid(queryIndex + 1));
		for (const auto &item : query.queryItems()) {
			if (item.first != "outline") {
				error = QStringLiteral("暂不支持 SS 插件或其他链接参数。");
				return false;
			}
		}
		body.truncate(queryIndex);
	}
	auto decode = [](const QString &value) {
		auto decoded = QByteArray::fromBase64Encoding(
			value.toLatin1(), QByteArray::Base64UrlEncoding | QByteArray::AbortOnBase64DecodingErrors);
		if (!decoded)
			decoded = QByteArray::fromBase64Encoding(value.toLatin1(),
								 QByteArray::AbortOnBase64DecodingErrors);
		return decoded ? QString::fromUtf8(decoded.decoded) : QString();
	};
	if (!body.contains('@'))
		body = decode(body);
	const int at = body.lastIndexOf('@');
	if (at <= 0)
		return false;
	QString auth = QUrl::fromPercentEncoding(body.left(at).toUtf8());
	if (!auth.contains(':'))
		auth = decode(auth);
	const int colon = auth.indexOf(':');
	if (colon <= 0 || colon == auth.size() - 1)
		return false;
	const QUrl endpoint("ss://" + body.mid(at + 1), QUrl::StrictMode);
	const int port = endpoint.port(-1);
	if (!endpoint.isValid() || endpoint.host().isEmpty() || !endpoint.userInfo().isEmpty() || port < 1 ||
	    port > 65535 || (!endpoint.path().isEmpty() && endpoint.path() != "/"))
		return false;
	node.method = auth.left(colon);
	const QStringList methods{"aes-128-gcm", "aes-192-gcm", "aes-256-gcm", "chacha20-ietf-poly1305",
				  "xchacha20-ietf-poly1305"};
	if (!methods.contains(node.method)) {
		error = QStringLiteral("不支持此 SS 加密方式，请使用 AES-GCM 或 ChaCha20-Poly1305 节点。");
		return false;
	}
	node.password = auth.mid(colon + 1);
	node.host = endpoint.host();
	node.port = static_cast<quint16>(port);
	error.clear();
	return true;
}

bool validate(const Settings &settings, QString &error)
{
	error.clear();
	if (!settings.enabled)
		return true;
	if (!settings.api && !settings.stream) {
		error = QStringLiteral("启用代理后，请至少选择一个适用范围。");
		return false;
	}
	return validateNode(settings, error);
}

bool validateNode(const Settings &settings, QString &error)
{
	if (settings.backend == "ss") {
		Node node;
		return parseUri(settings.uri, node, error);
	}
	if (settings.backend != "openvpn") {
		error = QStringLiteral("未知代理类型。");
		return false;
	}
	QJsonObject endpoint;
	bool requiresPassword = false;
	if (!parseOpenVpn(settings.ovpnProfile, endpoint, requiresPassword, error))
		return false;
	if (requiresPassword && (settings.ovpnUsername.isEmpty() || settings.ovpnPassword.isEmpty())) {
		error = QStringLiteral("此 OVPN 配置需要 VPN 用户名和密码，请在下方填写。");
		return false;
	}
	if (settings.ovpnUsername.size() > 1024 || settings.ovpnPassword.size() > 1024) {
		error = QStringLiteral("VPN 账号或密码过长。");
		return false;
	}
	return true;
}

bool validateChange(const Settings &current, const Settings &candidate, bool outputActive, bool operationActive,
		    bool managedSession, QString &error)
{
	if (outputActive || operationActive) {
		error = QStringLiteral("OBS 推流或接口操作进行中，无法修改代理。");
		return false;
	}
	if (managedSession && (current.enabled != candidate.enabled || current.api != candidate.api ||
			       current.stream != candidate.stream)) {
		error = QStringLiteral(
			"请先结束 B 站托管直播，再更改代理开关或适用范围。OBS 停止后仍可更换节点并保存，以恢复连接。");
		return false;
	}
	return validate(candidate, error);
}

bool saveSettings(const QString &path, const Settings &settings, QString &error)
{
	if (!validate(settings, error))
		return false;
	std::string secret, message;
	const QJsonObject credentials{{"uri", settings.uri},
				      {"profile", settings.ovpnProfile},
				      {"username", settings.ovpnUsername},
				      {"password", settings.ovpnPassword}};
	if (!Core::protectSecret(QJsonDocument(credentials).toJson(QJsonDocument::Compact).toStdString(), secret,
				 message)) {
		error = QStringLiteral("Windows 无法加密代理配置。");
		return false;
	}
	const QJsonObject object{{"version", 2},
				 {"enabled", settings.enabled},
				 {"api", settings.api},
				 {"stream", settings.stream},
				 {"keep_connected", settings.keepConnected},
				 {"backend", settings.backend},
				 {"credentials_protected", QString::fromStdString(secret)}};
	QDir().mkpath(QFileInfo(path).absolutePath());
	QSaveFile file(path);
	const QByteArray bytes = QJsonDocument(object).toJson();
	if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
		error = QStringLiteral("无法保存代理设置。");
		return false;
	}
	QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
	return true;
}

bool loadSettings(const QString &path, Settings &settings, QString &error)
{
	settings = {};
	error.clear();
	if (!QFileInfo::exists(path))
		return true;
	// Corrupt/unreadable saved routing must not silently become direct routing.
	settings.enabled = true;
	error = QStringLiteral("代理设置无法读取或解密；已阻止直连，请重新保存代理设置。");
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly) || file.size() > 2 * 1024 * 1024)
		return false;
	QJsonParseError parseError;
	const auto doc = QJsonDocument::fromJson(file.readAll(), &parseError);
	if (parseError.error != QJsonParseError::NoError || !doc.isObject())
		return false;
	const auto object = doc.object();
	const int version = object["version"].toInt();
	if (object.contains("keep_connected") && !object["keep_connected"].isBool())
		return false;
	if ((version != 1 && version != 2) || !object["enabled"].isBool() || !object["api"].isBool() ||
	    !object["stream"].isBool())
		return false;
	std::string plain, message;
	if (!Core::unprotectSecret(
		    object[version == 1 ? "uri_protected" : "credentials_protected"].toString().toStdString(), plain,
		    message))
		return false;
	Settings candidate{object["enabled"].toBool(), object["api"].toBool(), object["stream"].toBool(), QString()};
	candidate.keepConnected = object["keep_connected"].toBool(false);
	if (version == 1)
		candidate.uri = QString::fromStdString(plain);
	else {
		const auto credentials = QJsonDocument::fromJson(QByteArray::fromStdString(plain));
		if (!credentials.isObject())
			return false;
		const auto data = credentials.object();
		candidate.backend = object["backend"].toString();
		if (candidate.backend != "ss" && candidate.backend != "openvpn")
			return false;
		candidate.uri = data["uri"].toString();
		candidate.ovpnProfile = data["profile"].toString();
		candidate.ovpnUsername = data["username"].toString();
		candidate.ovpnPassword = data["password"].toString();
	}
	if (!validate(candidate, error))
		return false;
	settings = candidate;
	error.clear();
	return true;
}

QJsonObject coreConfig(const Node &node, quint16 port, const QString &user, const QString &password)
{
	const QJsonObject inbound{{"type", "socks"},
				  {"tag", "local"},
				  {"listen", "127.0.0.1"},
				  {"listen_port", int(port)},
				  {"users", QJsonArray{QJsonObject{{"username", user}, {"password", password}}}}};
	const QJsonObject outbound{{"type", "shadowsocks"}, {"tag", "ss"},
				   {"server", node.host},   {"server_port", int(node.port)},
				   {"method", node.method}, {"password", node.password},
				   {"network", "tcp"}};
	return {{"log", QJsonObject{{"disabled", true}}},
		{"inbounds", QJsonArray{inbound}},
		{"outbounds", QJsonArray{outbound}},
		{"route", QJsonObject{{"final", "ss"}}}};
}

QJsonObject coreConfig(const Settings &settings, quint16 port, const QString &user, const QString &password,
		       QString &error)
{
	if (!validateNode(settings, error))
		return {};
	Node node;
	if (settings.backend == "ss") {
		parseUri(settings.uri, node, error);
		return coreConfig(node, port, user, password);
	}
	QJsonObject endpoint;
	bool requiresPassword = false;
	if (!parseOpenVpn(settings.ovpnProfile, endpoint, requiresPassword, error))
		return {};
	endpoint["username"] = settings.ovpnUsername;
	endpoint["password"] = settings.ovpnPassword;
	endpoint["domain_resolver"] = "bootstrap";
	QJsonObject config = coreConfig(node, port, user, password);
	config.remove("outbounds");
	config["endpoints"] = QJsonArray{endpoint};
	config["dns"] = QJsonObject{{"servers", QJsonArray{QJsonObject{{"type", "local"}, {"tag", "bootstrap"}},
							   QJsonObject{{"type", "openvpn"},
								       {"tag", "vpn-dns"},
								       {"endpoint", "ovpn"},
								       {"accept_default_resolvers", true}}}},
				    {"final", "vpn-dns"},
				    {"strategy", "ipv4_only"}};
	config["route"] = QJsonObject{{"final", "ovpn"}, {"default_domain_resolver", "vpn-dns"}};
	return config;
}
} // namespace Proxy
