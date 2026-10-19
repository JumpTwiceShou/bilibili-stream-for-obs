#include "proxy/proxy_settings.hpp"
#include "proxy/openvpn_status.hpp"
#include <QJsonDocument>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>
#include <iostream>

static int failures = 0;
static void expect(bool condition, const char *message)
{
	if (!condition) {
		std::cerr << "FAIL: " << message << '\n';
		++failures;
	}
}
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	expect(Proxy::parseOpenVpnStatus("State: connected\n").state == "connected",
	       "parse the pinned core connected status");
	expect(Proxy::parseOpenVpnStatus("State: error\nError: authentication failed: terminal (auth-retry none)\n")
		       .authenticationRejected,
	       "classify real terminal authentication failure without exposing its text");
	expect(!Proxy::parseOpenVpnStatus("State: connecting\nError: previous authentication failed\n")
			.authenticationRejected,
	       "a previous error cannot reject an ongoing new handshake");
	expect(Proxy::parseOpenVpnStatus("State: error\nError: x509: certificate signed by unknown authority\n")
		       .certificateRejected,
	       "classify terminal CA rejection separately from retryable disconnection");
	Proxy::Node node;
	QString error;
	const QString profile =
		"client\ndev tun\nproto udp\nremote vpn.example 1194\nauth-user-pass\n"
		"ns-cert-type server\nsetenv CLIENT_CERT 0\nredirect-gateway def1\n<ca>\nfixture-ca\n</ca>\n";
	QJsonObject vpn;
	bool needsPassword = false;
	expect(Proxy::parseOpenVpn(profile, vpn, needsPassword, error), "import inline-CA UDP OpenVPN profile");
	expect(needsPassword && vpn["network"] == "udp" && vpn["system"] == false,
	       "OpenVPN requires credentials and uses internal stack");
	expect(vpn["tls"].toObject()["ns_certificate_type"] == "server" &&
		       vpn["tls"].toObject()["remote_certificate_tls"] == "none" &&
		       vpn["tls"].toObject().contains("certificate"),
	       "legacy purpose policy retains CA and Netscape verification without adding modern KU requirements");
	expect(Proxy::parseOpenVpn(profile + "remote-cert-tls server\n", vpn, needsPassword, error) &&
		       vpn["tls"].toObject()["remote_certificate_tls"] == "server" &&
		       vpn["tls"].toObject()["ns_certificate_type"] == "server",
	       "explicit modern certificate purpose is never relaxed");
	QString tcp = profile;
	tcp.replace("proto udp", "proto tcp");
	expect(Proxy::parseOpenVpn(tcp, vpn, needsPassword, error) && vpn["network"] == "tcp",
	       "import TCP OpenVPN profile");
	expect(!Proxy::parseOpenVpn(profile + "up execute-untrusted.exe\n", vpn, needsPassword, error),
	       "reject configuration script execution");
	expect(!Proxy::parseOpenVpn(profile + "config other.ovpn\n", vpn, needsPassword, error),
	       "reject nested arbitrary file access");
	expect(!Proxy::parseOpenVpn(profile + "ca external.pem\n", vpn, needsPassword, error),
	       "require self-contained certificate files");
	expect(!Proxy::parseOpenVpn(profile + "unknown-option secret-value\n", vpn, needsPassword, error) &&
		       !error.contains("secret-value"),
	       "unknown directives fail without leaking their values");
	const QString uri = "ss://" +
			    QString::fromLatin1(QByteArray("chacha20-ietf-poly1305:test:password").toBase64()) +
			    "@node.example:8388/?outline=1#Test%20node";
	expect(Proxy::parseUri(uri, node, error), "accept SIP002 and Outline marker");
	expect(node.host == "node.example" && node.port == 8388 && node.password == "test:password" &&
		       node.name == "Test node",
	       "preserve port, password colon and decoded label");
	expect(Proxy::parseUri("ss://" + QString::fromLatin1(QByteArray("aes-256-gcm:secret@[::1]:8388").toBase64()),
			       node, error) &&
		       node.host == "::1",
	       "legacy encoded URI supports IPv6");
	expect(!Proxy::parseUri("ss://invalid@node.example:0", node, error), "reject invalid base64 and zero port");
	expect(!Proxy::parseUri(uri + "\nignored", node, error), "reject embedded newline");
	expect(!Proxy::parseUri(uri.section('#', 0, 0) + "&plugin=unknown", node, error), "reject unsupported plugins");
	expect(!Proxy::parseUri("ss://bm9uZTpw@node.example:8388", node, error), "reject unencrypted method");
	Proxy::Settings settings;
	expect(settings.api && !settings.stream && !settings.keepConnected,
	       "new settings default to API-only on demand");
	expect(Proxy::validate(settings, error) && !settings.apiEnabled() && !settings.streamEnabled(),
	       "disabled proxy allows no node and routes both scopes directly");
	settings.enabled = true;
	settings.uri = uri;
	settings.stream = true;
	expect(Proxy::validate(settings, error) && settings.apiEnabled() && settings.streamEnabled(), "both scopes");
	settings.api = false;
	expect(Proxy::validate(settings, error) && !settings.apiEnabled() && settings.streamEnabled(), "stream only");
	settings.api = true;
	settings.stream = false;
	expect(Proxy::validate(settings, error) && settings.apiEnabled() && !settings.streamEnabled(), "API only");
	settings.api = false;
	expect(!Proxy::validate(settings, error), "enabled without scope is rejected");
	settings.api = true;
	Proxy::Settings replacement = settings;
	replacement.uri.replace("node.example", "replacement.example");
	expect(Proxy::validateChange(settings, replacement, false, false, true, error),
	       "allow node recovery while managed room remains live but OBS stopped");
	replacement.enabled = false;
	expect(!Proxy::validateChange(settings, replacement, false, false, true, error),
	       "do not change route scope during managed session recovery");
	expect(!Proxy::validateChange(settings, settings, true, false, false, error),
	       "lock changes during active output");
	expect(!Proxy::validateChange(settings, settings, false, true, false, error),
	       "lock changes during active API operation");
	expect(settings.onDemandApi() && !settings.keepRunning(), "API-only releases idle tunnel");
	settings.keepConnected = true;
	expect(!settings.onDemandApi() && settings.keepRunning(), "explicit keepalive holds tunnel");
	QTemporaryDir dir;
	const QString path = dir.filePath("proxy.json");
	expect(Proxy::saveSettings(path, settings, error), "save protected node");
	QFile saved(path);
	saved.open(QIODevice::ReadOnly);
	const auto bytes = saved.readAll();
	expect(!bytes.contains("test:password") && !bytes.contains("ss://") && bytes.contains("dpapi-v1:"),
	       "persist only Windows encrypted node");
	Proxy::Settings restored;
	expect(Proxy::loadSettings(path, restored, error) && restored.uri == uri && restored.enabled && restored.api &&
		       !restored.stream && restored.keepConnected,
	       "restore node and independent scopes");
	auto legacy = QJsonDocument::fromJson(bytes).object();
	legacy.remove("keep_connected");
	saved.close();
	saved.open(QIODevice::WriteOnly | QIODevice::Truncate);
	saved.write(QJsonDocument(legacy).toJson());
	saved.close();
	expect(Proxy::loadSettings(path, restored, error) && !restored.keepConnected && restored.uri == uri &&
		       !restored.stream,
	       "upgrade old proxy state without changing selected scopes or encrypted node");
	Proxy::parseUri(uri, node, error);
	const auto config = Proxy::coreConfig(node, 12345, "local-user", "local-password");
	const auto inbound = config["inbounds"].toArray().at(0).toObject();
	expect(inbound["listen"].toString() == "127.0.0.1" && inbound["listen_port"].toInt() == 12345 &&
		       inbound["users"].toArray().size() == 1,
	       "core binds authenticated loopback only");
	expect(config["outbounds"].toArray().size() == 1 &&
		       config["outbounds"].toArray().at(0).toObject()["type"].toString() == "shadowsocks",
	       "core has no direct fallback outbound");
	settings.backend = "openvpn";
	settings.ovpnProfile = profile;
	expect(!Proxy::validate(settings, error), "OVPN requires account before enabling");
	settings.ovpnUsername = "fixture-user";
	settings.ovpnPassword = "fixture-password";
	expect(Proxy::validate(settings, error), "OVPN allows supplied username password");
	const auto vpnConfig = Proxy::coreConfig(settings, 12345, "local-user", "local-password", error);
	expect(vpnConfig["endpoints"].toArray().size() == 1 && !vpnConfig.contains("outbounds") &&
		       vpnConfig["route"].toObject()["final"] == "ovpn",
	       "OpenVPN routes to isolated endpoint with no direct fallback");
	const auto dns = vpnConfig["dns"].toObject();
	const auto businessDns = dns["servers"].toArray()[1].toObject();
	expect(dns["final"] == "vpn-dns" && businessDns["type"] == "openvpn" && businessDns["endpoint"] == "ovpn" &&
		       businessDns["accept_default_resolvers"] == true,
	       "business DNS uses provider-pushed resolvers through VPN without local fallback");
	saved.close();
	expect(Proxy::saveSettings(path, settings, error), "save encrypted VPN credentials and certificate profile");
	saved.close();
	saved.open(QIODevice::ReadOnly);
	const auto vpnBytes = saved.readAll();
	expect(!vpnBytes.contains("fixture-user") && !vpnBytes.contains("fixture-password") &&
		       !vpnBytes.contains("fixture-ca") && !vpnBytes.contains("vpn.example"),
	       "VPN credentials profile never saved as plaintext");
	expect(Proxy::loadSettings(path, restored, error) && restored.backend == "openvpn" &&
		       restored.ovpnProfile == profile && restored.ovpnUsername == settings.ovpnUsername &&
		       restored.ovpnPassword == settings.ovpnPassword,
	       "VPN encrypted roundtrip preserves credentials profile");
	std::cout << "Proxy tests: " << failures << " failure(s)\n";
	return failures ? 1 : 0;
}
