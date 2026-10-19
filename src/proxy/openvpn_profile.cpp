#include "proxy_settings.hpp"
#include <QJsonArray>
#include <QMap>
#include <QRegularExpression>
namespace Proxy {
bool parseOpenVpn(const QString &profile, QJsonObject &endpoint, bool &requiresPassword, QString &error)
{
	endpoint = {};
	requiresPassword = false;
	error = QStringLiteral("OVPN 配置无效；请导入包含内嵌 CA 的 TUN 客户端配置。");
	if (profile.isEmpty() || profile.size() > 256 * 1024 || profile.contains(QChar(0)))
		return false;
	QJsonObject tls, options;
	QMap<QString, QStringList> directives;
	QString block, contents;
	bool client = false;
	for (QString line : profile.split('\n')) {
		line = line.trimmed();
		if (!block.isEmpty()) {
			if (line == "</" + block + ">") {
				if (contents.isEmpty())
					return false;
				const QString field = block == "ca"     ? "certificate"
						      : block == "cert" ? "client_certificate"
									: "client_key";
				if (tls.contains(field))
					return false;
				tls[field] = QJsonArray{contents};
				block.clear();
				contents.clear();
			} else {
				contents += line + '\n';
			}
			continue;
		}
		if (line.isEmpty() || line.startsWith('#') || line.startsWith(';'))
			continue;
		if (line == "<ca>" || line == "<cert>" || line == "<key>") {
			block = line.mid(1, line.size() - 2);
			continue;
		}
		// Never execute profile directives or follow paths to another local file.
		QStringList words = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
		for (int i = 1; i < words.size(); ++i)
			if (words[i].startsWith('#') || words[i].startsWith(';')) {
				words = words.mid(0, i);
				break;
			}
		const QString name = words.takeFirst();
		if (name == "client" && words.isEmpty()) {
			client = true;
			continue;
		}
		if ((name == "nobind" || name == "persist-key" || name == "persist-tun" || name == "auth-nocache" ||
		     name == "pull") &&
		    words.isEmpty())
			continue;
		if (name == "verb" && words.size() == 1 && words[0].toInt() >= 0 && words[0].toInt() <= 6)
			continue;
		if (name == "resolv-retry" && words == QStringList{"infinite"})
			continue;
		if (name == "setenv" &&
		    (words == QStringList{"CLIENT_CERT", "0"} || words == QStringList{"opt", "block-outside-dns"}))
			continue;
		if (name == "redirect-gateway" && words == QStringList{"def1"})
			continue; // Scope is controlled by this plugin.
		const QStringList supported{"dev",
					    "dev-type",
					    "proto",
					    "remote",
					    "port",
					    "auth-user-pass",
					    "ns-cert-type",
					    "remote-cert-tls",
					    "auth",
					    "cipher",
					    "data-ciphers",
					    "data-ciphers-fallback",
					    "tls-version-min",
					    "tls-version-max",
					    "tun-mtu",
					    "mssfix",
					    "ping",
					    "ping-restart",
					    "reneg-sec"};
		if (!supported.contains(name) || directives.contains(name)) {
			error = QStringLiteral(
				"OVPN 包含不支持或重复的指令；此版本不执行脚本、不读取外部证书文件，且只接受一个 remote。");
			return false;
		}
		directives[name] = words;
	}
	if (!block.isEmpty() || !client || directives.value("dev") != QStringList{"tun"} ||
	    !tls.contains("certificate"))
		return false;
	if (directives.contains("dev-type") && directives["dev-type"] != QStringList{"tun"})
		return false;
	if (tls.contains("client_certificate") != tls.contains("client_key"))
		return false;
	if (directives.contains("auth-user-pass")) {
		if (!directives["auth-user-pass"].isEmpty()) {
			error = QStringLiteral("请在设置界面填写 VPN 账号密码，不支持配置中的账号文件路径。");
			return false;
		}
		requiresPassword = true;
	}
	auto protocol = [](QString value) {
		if (value.endsWith("-client"))
			value.chop(7);
		return value;
	};
	QString network = "udp";
	if (directives.contains("proto")) {
		if (directives["proto"].size() != 1)
			return false;
		network = protocol(directives["proto"][0]);
	}
	int port = 1194;
	if (directives.contains("port")) {
		if (directives["port"].size() != 1)
			return false;
		port = directives["port"][0].toInt();
	}
	const auto remote = directives.value("remote");
	if (remote.isEmpty() || remote.size() > 3 || remote[0].startsWith('-') || remote[0].contains('/') ||
	    remote[0].contains('"'))
		return false;
	if (remote.size() >= 2)
		port = remote[1].toInt();
	if (remote.size() == 3)
		network = protocol(remote[2]);
	if (port < 1 || port > 65535 || !QStringList{"tcp", "udp", "tcp4", "udp4", "tcp6", "udp6"}.contains(network))
		return false;
	for (const QString &name : {QString("ns-cert-type"), QString("remote-cert-tls")}) {
		if (directives.contains(name)) {
			if (directives[name] != QStringList{"server"})
				return false;
			tls[name == "ns-cert-type" ? "ns_certificate_type" : "remote_certificate_tls"] = "server";
		}
	}
	// The core defaults to modern KU/EKU verification. A legacy profile explicitly
	// requesting Netscape server purpose must retain that policy, not gain an extra
	// requirement its certificate may not contain. CA and Netscape checks stay on.
	if (directives.contains("ns-cert-type") && !directives.contains("remote-cert-tls"))
		tls["remote_certificate_tls"] = "none";
	const QMap<QString, QString> strings{{"auth", "auth"},
					     {"cipher", "data_ciphers_fallback"},
					     {"data-ciphers-fallback", "data_ciphers_fallback"}};
	for (auto it = strings.begin(); it != strings.end(); ++it)
		if (directives.contains(it.key())) {
			const auto args = directives[it.key()];
			if (args.size() != 1 || args[0].compare("none", Qt::CaseInsensitive) == 0)
				return false;
			options[it.value()] = args[0];
		}
	if (directives.contains("data-ciphers")) {
		if (directives["data-ciphers"].size() != 1)
			return false;
		const auto ciphers = directives["data-ciphers"][0].split(':');
		for (const auto &cipher : ciphers)
			if (cipher.isEmpty() || cipher.compare("none", Qt::CaseInsensitive) == 0)
				return false;
		options["data_ciphers"] = QJsonArray::fromStringList(ciphers);
	}
	for (const QString &name : {QString("tls-version-min"), QString("tls-version-max")})
		if (directives.contains(name)) {
			if (directives[name].size() != 1 || !QStringList{"1.2", "1.3"}.contains(directives[name][0]))
				return false;
			tls[name == "tls-version-min" ? "version_min" : "version_max"] = directives[name][0];
		}
	const QMap<QString, QString> numbers{{"tun-mtu", "mtu"},
					     {"mssfix", "mss_fix"},
					     {"ping", "ping_interval"},
					     {"ping-restart", "ping_restart"},
					     {"reneg-sec", "renegotiate_interval"}};
	for (auto it = numbers.begin(); it != numbers.end(); ++it)
		if (directives.contains(it.key())) {
			bool ok = false;
			const auto args = directives[it.key()];
			int value = args.size() == 1 ? args[0].toInt(&ok) : -1;
			if (!ok || value < 0 || value > 86400)
				return false;
			if (it.key() == "tun-mtu" || it.key() == "mssfix") {
				if (value < 576 || value > 9000)
					return false;
				options[it.value()] = value;
			} else if (value == 0) {
				error = QStringLiteral("此版本不支持关闭 OpenVPN 保活或密钥更新。");
				return false;
			} else
				options[it.value()] = QString::number(value) + "s";
		}
	options["type"] = "openvpn-client";
	options["tag"] = "ovpn";
	options["system"] = false;
	options["mode"] = "tls";
	options["network"] = network;
	options["server"] = remote[0];
	options["server_port"] = port;
	options["tls"] = tls;
	options["auth_retry"] = "none";
	endpoint = options;
	error.clear();
	return true;
}
} // namespace Proxy
