#include "http_client.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTcpServer>
#include <QThread>
#include <atomic>
#include <curl/curl.h>
#include <iostream>

namespace {
int failures = 0;
void check(bool condition, const char *description)
{
	if (!condition) {
		++failures;
		std::cerr << "FAIL: " << description << '\n';
	}
}
bool waitFor(const std::function<bool()> &condition, int timeout)
{
	QElapsedTimer timer;
	timer.start();
	while (!condition() && timer.elapsed() < timeout) {
		QCoreApplication::processEvents();
		QThread::msleep(2);
	}
	return condition();
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	for (int cycle = 0; cycle < 3; ++cycle) {
		QTcpServer stalled;
		stalled.setProxy(QNetworkProxy::NoProxy);
		check(stalled.listen(QHostAddress::LocalHost), "start local stalled transport fixture");
		check(Http::HttpClient::init(), "initialize or reinitialize HTTP worker");
		Http::HttpClient::setProxy(false, {});
		Http::HttpResponse response;
		std::atomic_bool finished{false};
		std::atomic_int queuedExecuted{0};
		check(Http::HttpClient::enqueue([&] {
			      response = Http::HttpClient::get(
				      "https://127.0.0.1:" + std::to_string(stalled.serverPort()), {}, 30000);
			      finished.store(true);
		      }),
		      "start an HTTPS request whose peer never completes TLS");
		check(waitFor([&] { return stalled.hasPendingConnections(); }, 3000), "request reaches stalled peer");
		for (int i = 0; i < 40; ++i)
			Http::HttpClient::enqueue([&] { ++queuedExecuted; });
		QElapsedTimer shutdown;
		shutdown.start();
		Http::HttpClient::cleanup();
		check(shutdown.elapsed() < 2500,
		      "shutdown cancels in-flight transfer without waiting for 30 second timeout");
		check(finished.load() && response.cancelled, "request unwinds as cancelled before worker is joined");
		check(queuedExecuted.load() == 0, "shutdown discards queued jobs instead of draining them");
		check(!Http::HttpClient::enqueue([] {}), "unloading worker refuses new jobs");
		Http::HttpClient::cleanup();
		std::cout << "Shutdown cycle " << cycle + 1 << " cancelled in " << shutdown.elapsed() << " ms\n";
	}
	check(Http::HttpClient::init(), "restart for unavailable VPN and ticket safety checks");
	Http::HttpClient::setProxy(true, {});
	std::atomic_bool done{false};
	Http::HttpResponse deadProxy;
	Http::HttpClient::enqueue([&] {
		deadProxy = Http::HttpClient::get("https://127.0.0.1:1", {}, 30000);
		done.store(true);
	});
	check(waitFor([&] { return done.load(); }, 1000) && deadProxy.status == 0 && !deadProxy.data.empty(),
	      "disconnected VPN resolves promptly without direct fallback");
	Http::HttpClient::setProxy(false, {});
	QTcpServer stalledProxy;
	stalledProxy.setProxy(QNetworkProxy::NoProxy);
	check(stalledProxy.listen(QHostAddress::LocalHost), "start stalled SOCKS fixture");
	Proxy::Endpoint endpoint;
	endpoint.port = stalledProxy.serverPort();
	endpoint.user = "fixture-user";
	endpoint.password = "fixture-password";
	endpoint.alive->store(true);
	Http::HttpResponse interrupted;
	done.store(false);
	Http::HttpClient::enqueue([&] {
		interrupted = Http::HttpClient::getViaProxy("https://fixture.invalid/", endpoint, {}, 12000);
		done.store(true);
	});
	check(waitFor([&] { return stalledProxy.hasPendingConnections(); }, 3000),
	      "proxy request reaches its local endpoint");
	endpoint.alive->store(false);
	check(waitFor([&] { return done.load(); }, 2500), "invalidating VPN endpoint cancels its in-flight request");
	Http::HttpClient::cleanup();
	check(interrupted.status == 0 && interrupted.networkError == CURLE_ABORTED_BY_CALLBACK,
	      "interrupted VPN transfer returns failure rather than starting a direct retry");
	check(Http::HttpClient::init(), "reinitialize after interrupted VPN cleanup");
	Http::HttpClient::setProxy(false, {});
	const auto untrusted =
		Http::HttpClient::getLoginTicket("https://passport.biligame.com.attacker.invalid/ticket");
	check(untrusted.status == 0 && untrusted.data.find("不受信任") != std::string::npos,
	      "ticket requests reject untrusted initial hosts before network dispatch");
	Http::HttpClient::cleanup();
	std::cout << "HTTP lifecycle: " << failures << " failure(s)\n";
	return failures ? 1 : 0;
}
