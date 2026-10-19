#include "proxy/proxy_runtime.hpp"
#include "proxy/api_request_gate.hpp"
#include "proxy/stream_relay.hpp"
#include "http_client.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkInterface>
#include <QNetworkReply>
#include <QProcess>
#include <QFile>
#include <QThread>
#include <QTcpServer>
#include <QSslSocket>
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#endif

static bool waitFor(const std::function<bool()> &condition, int timeout = 8000)
{
	QElapsedTimer clock;
	clock.start();
	while (!condition() && clock.elapsed() < timeout) {
		QCoreApplication::processEvents();
		QThread::msleep(2);
	}
	QCoreApplication::processEvents();
	return condition();
}
static quint16 freePort()
{
	QTcpServer server;
	server.listen(QHostAddress::LocalHost);
	return server.serverPort();
}
static QByteArray amf(const QByteArray &value)
{
	QByteArray b(1, char(2));
	b += char(value.size() >> 8);
	b += char(value.size());
	return b + value;
}
static QByteArray command(const QByteArray &url)
{
	QByteArray b = amf("connect") + QByteArray::fromHex("003ff0000000000000030005746355726c") + amf(url) +
		       QByteArray::fromHex("000009");
	QByteArray p = QByteArray::fromHex("030000000000001400000000");
	p[6] = char(b.size());
	return p + b;
}
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	if (argc < 2)
		return 2;
	if (QString::fromLocal8Bit(argv[1]) == "--tls-diagnostic") {
		std::cout << "TLS supported=" << QSslSocket::supportsSsl()
			  << " backend=" << QSslSocket::activeBackend().toStdString() << std::endl;
		QNetworkAccessManager network;
		network.setProxy(QNetworkProxy::NoProxy);
		QNetworkRequest request{QUrl("https://www.cloudflare.com/cdn-cgi/trace")};
		request.setTransferTimeout(10000);
		auto *reply = network.get(request);
		waitFor([&] { return reply->isFinished(); }, 12000);
		std::cout << "QT_HTTPS error=" << int(reply->error())
			  << " detail=" << reply->errorString().toStdString() << std::endl;
		reply->abort();
		delete reply;
		if (Http::HttpClient::init()) {
			Http::HttpClient::setProxy(false, {});
			const auto response =
				Http::HttpClient::get("https://www.cloudflare.com/cdn-cgi/trace", {}, 12000);
			std::cout << "CURL_HTTPS status=" << response.status << " timeout=" << response.timeout
				  << std::endl;
			Http::HttpClient::cleanup();
		}
		return 0;
	}
	const QString core = QString::fromLocal8Bit(argv[1]);
	if (argc > 2 && (QString::fromLocal8Bit(argv[2]) == "--openvpn-node-stdin" ||
			 QString::fromLocal8Bit(argv[2]) == "--openvpn-saved-diagnostic")) {
		Proxy::Settings settings;
		const bool diagnostic = QString::fromLocal8Bit(argv[2]) == "--openvpn-saved-diagnostic";
		if (diagnostic) {
			QString error;
			if (argc < 4 || !Proxy::loadSettings(QString::fromLocal8Bit(argv[3]), settings, error)) {
				std::cout << "SAVED_SETTINGS_UNAVAILABLE\n";
				return 1;
			}
			std::cout << "TLS supported=" << QSslSocket::supportsSsl()
				  << " backend=" << QSslSocket::activeBackend().toStdString() << std::endl;
		} else {
			const std::string input((std::istreambuf_iterator<char>(std::cin)),
						std::istreambuf_iterator<char>());
			const auto data = QJsonDocument::fromJson(QByteArray::fromStdString(input)).object();
			settings.backend = "openvpn";
			settings.ovpnProfile = data["profile"].toString();
			settings.ovpnUsername = data["username"].toString();
			settings.ovpnPassword = data["password"].toString();
		}
		Proxy::Runtime runtime(core);
		QElapsedTimer connectTime;
		connectTime.start();
		runtime.start(settings);
		if (!waitFor([&] { return !runtime.starting(); }, 65000) || !runtime.endpoint().ready()) {
			std::cout << "VPN_CONNECT_FAILED elapsed_ms=" << connectTime.elapsed() << '\n';
			return 1;
		}
		std::cout << "VPN_CONNECTED handshake_ms=" << connectTime.elapsed() << std::endl;
		if (diagnostic && Http::HttpClient::init()) {
			Http::HttpClient::setProxy(false, {});
			Http::HttpResponse response;
			std::atomic_bool completed{false};
			const auto endpoint = runtime.endpoint();
			Http::HttpClient::enqueue([&, endpoint] {
				response = Http::HttpClient::getViaProxy("https://www.cloudflare.com/cdn-cgi/trace",
									 endpoint, {}, 12000);
				completed.store(true);
			});
			waitFor([&] { return completed.load(); }, 15000);
			Http::HttpClient::cleanup();
			std::cout << "CURL_HTTPS status=" << response.status << " timeout=" << response.timeout
				  << std::endl;
		}
		QNetworkAccessManager network;
		network.setProxy(runtime.endpoint().networkProxy());
		for (int sample = 0; sample < 3; ++sample) {
			QNetworkRequest request{QUrl("https://www.cloudflare.com/cdn-cgi/trace")};
			request.setTransferTimeout(15000);
			request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
					     QNetworkRequest::ManualRedirectPolicy);
			QElapsedTimer elapsed;
			elapsed.start();
			auto *reply = network.get(request);
			waitFor([&] { return reply->isFinished(); }, 18000);
			const auto body = reply->read(32 * 1024);
			QByteArray region, ip;
			for (const auto &line : body.split('\n')) {
				if (line.startsWith("loc="))
					region = line.mid(4).trimmed();
				if (line.startsWith("ip="))
					ip = line.mid(3).trimmed();
			}
			if (!reply->isFinished() || reply->error() != QNetworkReply::NoError ||
			    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200 ||
			    QHostAddress(QString::fromLatin1(ip)).isNull() || region.size() != 2 ||
			    !runtime.endpoint().ready()) {
				std::cout << "VPN_HTTPS_FAILED sample=" << sample + 1
					  << " error=" << int(reply->error()) << '\n';
				if (diagnostic)
					std::cout << "QT_HTTPS_DETAIL " << reply->errorString().toStdString()
						  << std::endl;
				reply->abort();
				delete reply;
				return 1;
			}
			std::cout << "VPN_HTTPS_OK sample=" << sample + 1 << " latency_ms=" << elapsed.elapsed()
				  << " region=" << region.constData() << " ip=" << ip.constData() << std::endl;
			delete reply;
		}
		runtime.stop();
		return 0;
	}
	if (argc > 3 && QString::fromLocal8Bit(argv[2]) == "--check-profile") {
		QFile file(QString::fromLocal8Bit(argv[3]));
		if (!file.open(QIODevice::ReadOnly))
			return 2;
		Proxy::Settings settings;
		settings.backend = "openvpn";
		settings.ovpnProfile = QString::fromUtf8(file.readAll());
		settings.ovpnUsername = "validation-only";
		settings.ovpnPassword = "validation-only";
		QString error;
		auto config = Proxy::coreConfig(settings, freePort(), "fixture", "fixture", error);
		if (config.isEmpty()) {
			std::cerr << "PROFILE_IMPORT_FAILED\n";
			return 1;
		}
		QProcess checker;
		checker.start(core, {"check", "-c", "stdin"});
		checker.write(QJsonDocument(config).toJson(QJsonDocument::Compact));
		checker.closeWriteChannel();
		if (!checker.waitForFinished(8000) || checker.exitCode() != 0) {
			checker.kill();
			checker.waitForFinished();
			std::cerr << "PROFILE_CORE_CHECK_FAILED\n";
			return 1;
		}
		std::cout << "PROFILE_IMPORT_AND_CORE_CHECK_OK (no connection attempted)\n";
		return 0;
	}
	if (argc > 2 && QString::fromLocal8Bit(argv[2]) == "--openvpn-fixture-stdin") {
		std::string fixtureInput((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
		const auto fixture = QJsonDocument::fromJson(QByteArray::fromStdString(fixtureInput)).object();
		int failures = 0;
		auto check = [&](bool condition, const char *message) {
			if (!condition) {
				++failures;
				std::cerr << "FAIL: " << message << '\n';
			}
		};
		for (const QString &transport : {QString("tcp"), QString("udp")}) {
			const auto vpnPort = freePort();
			const QJsonObject endpoint{
				{"type", "openvpn-server"},
				{"tag", "server"},
				{"listen", "127.0.0.1"},
				{"listen_port", int(vpnPort)},
				{"network", transport},
				{"system", false},
				{"address", QJsonArray{"10.201.0.1/24"}},
				{"topology", "subnet"},
				{"users",
				 QJsonArray{QJsonObject{{"username", "fixture"}, {"password", "fixture-password"}}}},
				{"tls", QJsonObject{{"certificate", QJsonArray{fixture["certificate"]}},
						    {"key", QJsonArray{fixture["key"]}},
						    {"verify_client_certificate", "none"}}}};
			const QJsonObject config{{"log", QJsonObject{{"disabled", true}}},
						 {"endpoints", QJsonArray{endpoint}},
						 {"outbounds",
						  QJsonArray{QJsonObject{{"type", "direct"}, {"tag", "direct"}}}},
						 {"route", QJsonObject{{"final", "direct"}}}};
			QProcess server;
			auto startServer = [&] {
				server.start(core, {"run", "-c", "stdin"});
				server.write(QJsonDocument(config).toJson(QJsonDocument::Compact));
				server.closeWriteChannel();
				check(server.waitForStarted(3000), "launch OpenVPN fixture server");
			};
			if (transport == "tcp")
				QTimer::singleShot(2000, &app, startServer);
			else
				startServer();
			Proxy::Settings settings;
			settings.backend = "openvpn";
			const QString purpose = fixture["legacy"].toBool() ? "ns-cert-type server"
									   : "remote-cert-tls server";
			settings.ovpnProfile = "client\ndev tun\nproto " + transport + "\nremote 127.0.0.1 " +
					       QString::number(vpnPort) + "\nauth-user-pass\n" + purpose +
					       "\nredirect-gateway def1\n<ca>\n" + fixture["certificate"].toString() +
					       "</ca>\n";
			settings.ovpnUsername = "fixture";
			settings.ovpnPassword = "fixture-password";
			Proxy::Runtime runtime(core);
			runtime.start(settings);
			if (transport == "tcp")
				check(!waitFor([&] { return runtime.endpoint().ready(); }, 1000) && runtime.starting(),
				      "local SOCKS readiness must wait for delayed VPN handshake");
			check(waitFor([&] { return !runtime.starting(); }) && runtime.endpoint().ready(),
			      "OpenVPN core starts with imported profile");
			QHostAddress targetAddress;
			for (const auto &address : QNetworkInterface::allAddresses())
				if (address.protocol() == QAbstractSocket::IPv4Protocol && !address.isLoopback() &&
				    !address.isLinkLocal()) {
					targetAddress = address;
					break;
				}
			QTcpServer target;
			target.setProxy(QNetworkProxy::NoProxy);
			check(!targetAddress.isNull() && target.listen(targetAddress),
			      "bind controlled receiver to this host's network address");
			QByteArray received;
			QObject::connect(&target, &QTcpServer::newConnection, &app, [&] {
				auto *socket = target.nextPendingConnection();
				QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
					received += socket->readAll();
					socket->write("vpn-reply");
				});
			});
			const QString upstream = "rtmp://" + targetAddress.toString() + ":" +
						 QString::number(target.serverPort()) + "/live";
			Proxy::StreamRelay relay;
			QString error;
			check(relay.start(upstream, runtime.endpoint(), error), "start OpenVPN RTMP relay");
			QTcpSocket client;
			client.setProxy(QNetworkProxy::NoProxy);
			client.connectToHost(QHostAddress::LocalHost, quint16(QUrl(relay.localUrl()).port()));
			check(waitFor([&] { return client.state() == QAbstractSocket::ConnectedState; }),
			      "connect to OpenVPN relay");
			QByteArray handshake(3073, '\0');
			handshake[0] = 3;
			client.write(handshake + command(relay.localUrl().toUtf8()));
			const auto expected = handshake + command(upstream.toUtf8());
			check(waitFor([&] { return received.size() >= expected.size(); }, 15000) &&
				      received == expected,
			      "OpenVPN TLS authentication and real encrypted RTMP forwarding");
			check(waitFor([&] { return client.bytesAvailable() > 0; }, 1000) &&
				      client.readAll().contains("vpn-reply"),
			      "OpenVPN upstream replies");
			check(runtime.endpoint().counters->uploaded.load() >= quint64(expected.size()),
			      "OpenVPN traffic counter");
			if (transport == "tcp" && !fixture["legacy"].toBool()) {
				settings.enabled = true;
				runtime.setAutoReconnect(true);
				// Restart with short fixture keepalive values, then interrupt the real TLS server.
				settings.ovpnProfile += "ping 1\nping-restart 3\n";
				runtime.start(settings);
				check(waitFor([&] { return runtime.endpoint().ready(); }, 12000),
				      "recovery fixture connects");
				const auto stale = runtime.endpoint();
				server.kill();
				server.waitForFinished();
				check(waitFor([&] { return !stale.ready(); }, 12000),
				      "VPN interruption invalidates API endpoint");
				startServer();
				check(waitFor([&] { return runtime.endpoint().ready(); }, 25000),
				      "VPN recovers automatically after server interruption");
				QTcpServer stopReceiver;
				stopReceiver.setProxy(QNetworkProxy::NoProxy);
				check(stopReceiver.listen(targetAddress), "bind simulated stop endpoint");
				int stopRequests = 0;
				QObject::connect(&stopReceiver, &QTcpServer::newConnection, &app, [&] {
					auto *socket = stopReceiver.nextPendingConnection();
					QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
						if (!socket->readAll().contains("POST /stop"))
							return;
						++stopRequests;
						socket->write(
							"HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nOK");
						socket->disconnectFromHost();
					});
				});
				QNetworkAccessManager requests;
				requests.setProxy(runtime.endpoint().networkProxy());
				QNetworkRequest stopRequest{QUrl("http://" + targetAddress.toString() + ":" +
								 QString::number(stopReceiver.serverPort()) + "/stop")};
				auto *reply = requests.post(stopRequest, QByteArray("room_id=fixture"));
				check(waitFor([&] { return reply->isFinished(); }, 10000) &&
					      reply->error() == QNetworkReply::NoError && stopRequests == 1,
				      "reconnected VPN delivers and acknowledges pending stop command");
				delete reply;
				runtime.setAutoReconnect(false);
			}
			runtime.stop();
			check(waitFor([&] {
				      return !relay.ready() && client.state() == QAbstractSocket::UnconnectedState;
			      }),
			      "OpenVPN stop closes output");
			if (!fixture["legacy"].toBool()) {
				auto missingPurpose = settings;
				missingPurpose.ovpnProfile.replace("remote-cert-tls server", "ns-cert-type server");
				runtime.start(missingPurpose);
				check(waitFor([&] { return !runtime.starting(); }, 12000) && runtime.endpoint().ready(),
				      "legacy server purpose accepts valid modern server certificates");
				runtime.stop();
			}
			auto wrongCa = settings;
			wrongCa.ovpnProfile.replace(fixture["certificate"].toString(),
						    fixture["untrusted_ca"].toString());
			runtime.start(wrongCa);
			check(waitFor([&] { return !runtime.starting(); }, 12000) && !runtime.endpoint().ready(),
			      "both legacy and modern profiles reject server certificates outside the configured CA");
			runtime.stop();
			settings.ovpnPassword = "wrong-fixture-password";
			runtime.setAutoReconnect(true);
			runtime.start(settings);
			check(waitFor([&] { return !runtime.starting(); }, 12000) && !runtime.endpoint().ready(),
			      "wrong VPN password never produces ready endpoint");
			check(runtime.hasFailure() && !runtime.starting() &&
				      runtime.status().contains(QStringLiteral("认证被服务器拒绝")),
			      "terminal authentication failure pauses automatic reconnect and retains actionable status");
			runtime.setAutoReconnect(false);
			runtime.stop();
			if (server.state() == QProcess::NotRunning)
				std::cerr << "Fixture server failed: " << server.readAllStandardError().constData()
					  << '\n';
			server.kill();
			server.waitForFinished();
			std::cout << "OpenVPN " << transport.toStdString() << " tested\n";
		}
		std::cout << "OpenVPN network tests: " << failures << " failure(s)\n";
		return failures ? 1 : 0;
	}
	if (argc > 2 && QString::fromLocal8Bit(argv[2]) == "--node-stdin") {
#ifdef _WIN32
		const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
		DWORD mode = 0;
		const bool console = GetConsoleMode(input, &mode);
		if (console)
			SetConsoleMode(input, mode & ~ENABLE_ECHO_INPUT);
#endif
		std::cout << "READY_FOR_NODE" << std::endl;
		std::string uri;
		std::getline(std::cin, uri);
#ifdef _WIN32
		if (console)
			SetConsoleMode(input, mode);
#endif
		Proxy::Runtime runtime(core);
		runtime.start(QString::fromStdString(uri));
		if (!waitFor([&] { return !runtime.starting(); }) || !runtime.endpoint().ready()) {
			std::cout << "CORE_FAILED\n";
			return 1;
		}
		QNetworkAccessManager network;
		network.setProxy(runtime.endpoint().networkProxy());
		QNetworkRequest request{QUrl("https://www.cloudflare.com/cdn-cgi/trace")};
		request.setTransferTimeout(15000);
		QElapsedTimer elapsed;
		elapsed.start();
		auto *reply = network.get(request);
		waitFor([&] { return reply->isFinished(); }, 18000);
		if (!reply->isFinished() || reply->error() != QNetworkReply::NoError) {
			std::cout << "NODE_TEST_FAILED error=" << int(reply->error()) << '\n';
			reply->abort();
			delete reply;
			return 1;
		}
		const auto body = reply->readAll();
		QByteArray region;
		for (const auto &line : body.split('\n'))
			if (line.startsWith("loc="))
				region = line.mid(4).trimmed();
		std::cout << "NODE_TEST_OK latency_ms=" << elapsed.elapsed() << " region=" << region.constData()
			  << '\n';
		delete reply;
		return 0;
	}
	int failures = 0;
	auto check = [&](bool value, const char *message) {
		if (!value) {
			++failures;
			std::cerr << "FAIL: " << message << '\n';
		}
	};
	const quint16 ssPort = freePort();
	const QJsonObject config{{"log", QJsonObject{{"disabled", true}}},
				 {"inbounds", QJsonArray{QJsonObject{{"type", "shadowsocks"},
								     {"listen", "127.0.0.1"},
								     {"listen_port", int(ssPort)},
								     {"method", "chacha20-ietf-poly1305"},
								     {"password", "integration-fixture"}}}},
				 {"outbounds", QJsonArray{QJsonObject{{"type", "direct"}}}}};
	QProcess server;
	server.start(core, {"run", "-c", "stdin"});
	server.write(QJsonDocument(config).toJson(QJsonDocument::Compact));
	server.closeWriteChannel();
	check(server.waitForStarted(3000), "start real local Shadowsocks server");
	Proxy::Runtime runtime(core);
	runtime.start("ss://" +
		      QString::fromLatin1(QByteArray("chacha20-ietf-poly1305:integration-fixture").toBase64()) +
		      "@127.0.0.1:" + QString::number(ssPort));
	check(waitFor([&] { return !runtime.starting(); }) && runtime.endpoint().ready(),
	      "real core authenticated SOCKS readiness");
	if (!runtime.endpoint().ready()) {
		std::cerr << runtime.status().toStdString() << '\n';
		server.kill();
		server.waitForFinished();
		return 1;
	}
	QTcpServer target;
	target.setProxy(QNetworkProxy::NoProxy);
	target.listen(QHostAddress::LocalHost);
	const QString upstream = "rtmp://127.0.0.1:" + QString::number(target.serverPort()) + "/live";
	QByteArray received;
	int targetConnections = 0;
	QObject::connect(&target, &QTcpServer::newConnection, &app, [&] {
		++targetConnections;
		auto *socket = target.nextPendingConnection();
		QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
			received += socket->readAll();
			socket->write("upstream-reply");
		});
	});
	Proxy::StreamRelay relay;
	QString error;
	check(relay.start(upstream, runtime.endpoint(), error), "start real stream relay");
	QTcpSocket client;
	client.setProxy(QNetworkProxy::NoProxy);
	client.connectToHost(QHostAddress::LocalHost, quint16(QUrl(relay.localUrl()).port()));
	check(waitFor([&] { return client.state() == QAbstractSocket::ConnectedState; }), "OBS-side client connects");
	QByteArray handshake(3073, '\0');
	handshake[0] = 3;
	const auto input = handshake + command(relay.localUrl().toUtf8());
	client.write(input);
	const auto expected = handshake + command(upstream.toUtf8());
	check(waitFor([&] { return received.size() >= expected.size(); }) && received == expected,
	      "actual encrypted SS transport forwards RTMP with original destination metadata");
	check(waitFor([&] { return client.bytesAvailable() > 0; }) && client.readAll().contains("upstream-reply"),
	      "server responses reach OBS client");
	check(runtime.endpoint().counters->uploaded.load() >= quint64(expected.size()) &&
		      runtime.endpoint().counters->downloaded.load() > 0,
	      "measure real forwarded traffic");
	const auto previousEndpoint = runtime.endpoint();
	const auto relayUrl = relay.localUrl();
	runtime.stop();
	check(waitFor([&] { return !relay.ready() && client.state() == QAbstractSocket::UnconnectedState; }),
	      "core stop closes active stream");
	check(!previousEndpoint.ready(), "stale endpoint becomes invalid");
	QTcpSocket second;
	second.setProxy(QNetworkProxy::NoProxy);
	second.connectToHost(QHostAddress::LocalHost, quint16(QUrl(relayUrl).port()));
	waitFor([&] { return second.state() == QAbstractSocket::UnconnectedState; }, 1000);
	check(targetConnections == 1, "core failure never opens a direct upstream connection");
	check(Http::HttpClient::init(), "initialize real HTTP client");
	std::atomic_bool done{false};
	auto httpAttempt = [&](bool proxyRequired, const Proxy::Endpoint &endpoint) {
		done.store(false);
		Http::HttpClient::setProxy(proxyRequired, endpoint);
		Http::HttpClient::enqueue([&] {
			Http::HttpClient::get("https://127.0.0.1:" + std::to_string(target.serverPort()), {}, 1000);
			done.store(true);
		});
		check(waitFor([&] { return done.load(); }, 2000), "HTTP attempt completes");
	};
	const int beforeHttp = targetConnections;
	Http::HttpClient::setProxy(false, {});
	const auto explicitResponse = Http::HttpClient::getViaProxy(
		"https://127.0.0.1:" + std::to_string(target.serverPort()), previousEndpoint, {}, 1000);
	check(explicitResponse.status == 0 && targetConnections == beforeHttp,
	      "explicit probe proxy blocks a dead endpoint even when normal API routing is direct");
	httpAttempt(true, previousEndpoint);
	check(targetConnections == beforeHttp, "API enabled with dead endpoint blocks all direct requests");
	httpAttempt(false, previousEndpoint);
	check(targetConnections == beforeHttp + 1,
	      "API scope disabled goes direct even if stream proxy endpoint is dead");
	qputenv("NO_PROXY", "*");
	runtime.start("ss://" +
		      QString::fromLatin1(QByteArray("chacha20-ietf-poly1305:wrong-fixture-password").toBase64()) +
		      "@127.0.0.1:" + QString::number(ssPort));
	check(waitFor([&] { return !runtime.starting(); }) && runtime.endpoint().ready(),
	      "local core ready even when remote password is wrong");
	httpAttempt(true, runtime.endpoint());
	check(targetConnections == beforeHttp + 1,
	      "API proxy overrides NO_PROXY and wrong node never falls back to direct");
	Http::HttpClient::cleanup();
	runtime.stop();
	{
		Proxy::Settings requestSettings;
		requestSettings.enabled = true;
		requestSettings.uri =
			"ss://" +
			QString::fromLatin1(QByteArray("chacha20-ietf-poly1305:integration-fixture").toBase64()) +
			"@127.0.0.1:" + QString::number(ssPort);
		Proxy::ApiRequestGate gate(
			runtime, [&] { return requestSettings; },
			[&](std::function<void()> task) {
				QTimer::singleShot(0, &app, std::move(task));
				return true;
			});
		int dispatched = 0;
		auto operation = [&] {
			check(runtime.endpoint().ready(), "API task waits for ready proxy endpoint");
			++dispatched;
		};
		gate.setHeld(true);
		check(gate.enqueue(operation), "enqueue on-demand start request");
		check(dispatched == 0, "offline proxy does not dispatch API request immediately");
		check(waitFor([&] { return dispatched == 1; }) && runtime.endpoint().ready(),
		      "face/login workflow can hold the tunnel between requests");
		gate.setHeld(false);
		check(!runtime.endpoint().ready() && !runtime.starting(),
		      "idle API-only connection stops without disrupting direct output");
		check(gate.enqueue(operation), "enqueue subsequent stop request");
		check(waitFor([&] { return dispatched == 2 && !gate.busy(); }) && !runtime.endpoint().ready(),
		      "next API request reconnects then releases the tunnel");
		requestSettings.keepConnected = true;
		gate.enqueue(operation);
		check(waitFor([&] { return dispatched == 3 && !gate.busy(); }) && runtime.endpoint().ready(),
		      "keep-connected mode retains ready tunnel");
		requestSettings.keepConnected = false;
		requestSettings.stream = true;
		gate.enqueue(operation);
		check(waitFor([&] { return dispatched == 4 && !gate.busy(); }) && runtime.endpoint().ready(),
		      "stream proxy stays connected regardless of API keepalive switch");
	}
	runtime.stop();
	server.kill();
	server.waitForFinished();
	std::cout << "Real network tests: " << failures << " failure(s)\n";
	return failures ? 1 : 0;
}
