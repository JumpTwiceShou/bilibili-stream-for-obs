#include <QApplication>
#include <QStringList>
#include <QTimer>
#include <cstdlib>
#include "proxy/proxy_runtime.hpp"
#include <QTcpServer>
#include <QProcess>
#include <QTemporaryDir>
#include <QFile>
#include <QElapsedTimer>
#include <QThread>
#include <cstdio>

namespace UI {
int runOfficialRiskUiTests(bool online, const Proxy::Endpoint &endpoint);
}

int main(int argc, char **argv)
{
	QApplication application(argc, argv);
	QTimer::singleShot(90000, [] { std::exit(2); });
	const auto arguments = application.arguments();
	Proxy::Endpoint endpoint;
	QProcess core;
	QTemporaryDir directory;
	if (arguments.contains("--proxy-sdk")) {
		const auto index = arguments.indexOf("--proxy-sdk");
		if (index + 1 >= arguments.size())
			return 2;
		QTcpServer reservation;
		if (!reservation.listen(QHostAddress::LocalHost))
			return 2;
		endpoint.port = reservation.serverPort();
		reservation.close();
		endpoint.user = "fixture-user";
		endpoint.password = "fixture-password";
		QFile config(directory.filePath("core.json"));
		if (!config.open(QIODevice::WriteOnly))
			return 2;
		config.write(
			QStringLiteral(
				R"({"log":{"disabled":true},"inbounds":[{"type":"socks","listen":"127.0.0.1","listen_port":%1,"users":[{"username":"fixture-user","password":"fixture-password"}]}],"outbounds":[{"type":"direct"}]})")
				.arg(endpoint.port)
				.toUtf8());
		config.close();
		core.start(arguments[index + 1], {"run", "-c", config.fileName()});
		if (!core.waitForStarted(5000))
			return 2;
		QElapsedTimer deadline;
		deadline.start();
		bool ready = false;
		while (deadline.elapsed() < 5000) {
			QTcpSocket probe;
			probe.connectToHost(QHostAddress::LocalHost, endpoint.port);
			if (probe.waitForConnected(100)) {
				ready = true;
				break;
			}
			QThread::msleep(20);
		}
		if (!ready)
			return 2;
	}
	const int result = UI::runOfficialRiskUiTests(
		arguments.contains("--official-sdk") || arguments.contains("--proxy-sdk"), endpoint);
	if (core.state() != QProcess::NotRunning) {
		core.terminate();
		if (!core.waitForFinished(3000)) {
			core.kill();
			core.waitForFinished(3000);
		}
	}
	return result;
}
