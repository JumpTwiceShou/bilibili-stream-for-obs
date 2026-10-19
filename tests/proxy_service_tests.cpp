#include "proxy/proxy_service.hpp"
#include "core/obs_service_guard.hpp"
#include "core/stream_routes.hpp"
#include "core/config_manager.hpp"
#include <obs-module.h>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QElapsedTimer>
#include <QTcpServer>
#include <obs-frontend-api.h>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <cstring>
#include <iostream>

static obs_service_t *current = nullptr;
static bool active = false;
static int saved = 0;
extern "C" {
obs_module_t *obs_current_module()
{
	return nullptr;
}
bool obs_frontend_streaming_active()
{
	return active;
}
obs_service_t *obs_frontend_get_streaming_service()
{
	return current;
}
void obs_frontend_set_streaming_service(obs_service_t *service)
{
	auto *old = current;
	current = obs_service_get_ref(service);
	obs_service_release(old);
}
void obs_frontend_save_streaming_service()
{
	++saved;
}
}

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir dir;
	if (!obs_startup("en-US", dir.path().toUtf8().constData(), nullptr))
		return 2;
	int failures = 0;
	auto check = [&](bool value, const char *message) {
		if (!value) {
			++failures;
			std::cerr << "FAIL: " << message << '\n';
		}
	};
	obs_service_info fixture{};
	fixture.id = "fixture";
	fixture.get_protocol = [](void *) {
		return "RTMP";
	};
	fixture.get_name = [](void *) {
		return "fixture";
	};
	fixture.create = [](obs_data_t *, obs_service_t *) -> void * {
		return new int(0);
	};
	fixture.destroy = [](void *data) {
		delete static_cast<int *>(data);
	};
	fixture.get_url = [](void *) {
		return "rtmp://fixture.example/original";
	};
	fixture.get_key = [](void *) {
		return "original-fixture-key";
	};
	obs_register_service(&fixture);
	fixture.id = "rtmp_custom";
	obs_register_service(&fixture);
	Proxy::registerService();
	Core::registerManagedService();
	auto *original = obs_service_create("fixture", "original", nullptr, nullptr);
	auto *replacement = obs_service_create("fixture", "replacement", nullptr, nullptr);
	obs_frontend_set_streaming_service(original);
	Proxy::Endpoint endpoint;
	endpoint.port = 1234;
	endpoint.alive->store(true);
	QString error;
	{
		Proxy::ServiceGuard guard;
		check(guard.install("rtmp://127.0.0.1:1234/live", "private-fixture-key", endpoint, error),
		      "install temporary service");
		check(guard.ownsCurrent() && current != original && obs_service_can_try_to_connect(current),
		      "ready owned service can connect");
		check(std::strcmp(obs_service_get_connect_info(current, OBS_SERVICE_CONNECT_INFO_STREAM_KEY),
				  "private-fixture-key") == 0,
		      "stream key supplied to OBS in memory");
		auto *data = obs_service_get_settings(current);
		const char *json = obs_data_get_json(data);
		check(!json || !std::strstr(json, "private-fixture-key"),
		      "serialized settings do not contain stream key");
		obs_data_release(data);
		endpoint.alive->store(false);
		check(!obs_service_can_try_to_connect(current), "core failure blocks native OBS start");
		endpoint.alive->store(true);
		guard.invalidate();
		check(!obs_service_can_try_to_connect(current), "relay failure blocks native OBS start");
		active = true;
		auto *owned = current;
		Proxy::Endpoint recovered;
		recovered.port = 5678;
		recovered.alive->store(true);
		check(guard.install("rtmp://127.0.0.1:5678/live", "recovered-key", recovered, error) &&
			      current == owned && obs_service_can_try_to_connect(current),
		      "recovery updates the owned active service without replacing it");
		check(std::strcmp(obs_service_get_connect_info(current, OBS_SERVICE_CONNECT_INFO_SERVER_URL),
				  "rtmp://127.0.0.1:5678/live") == 0,
		      "OBS reconnect obtains the new relay address");
		active = false;
		guard.restore();
		check(current == original && saved == 1, "restore and persist original service");
		active = true;
		check(!guard.install({}, {}, endpoint, error) && current == original,
		      "do not replace service during streaming");
		active = false;
		check(guard.install("rtmp://127.0.0.1:1234/live", "fixture-key", endpoint, error), "reinstall service");
		obs_frontend_set_streaming_service(replacement);
		guard.restore();
		check(current == replacement && saved == 1, "respect user-changed service ownership");
	}
	{
		Core::ObsServiceGuard direct;
		check(direct.install("rtmp://fixture.example/live", "direct-fixture-key"),
		      "install direct managed service");
		check(obs_service_can_try_to_connect(current) &&
			      std::strcmp(obs_service_get_connect_info(current, OBS_SERVICE_CONNECT_INFO_STREAM_KEY),
					  "direct-fixture-key") == 0,
		      "native start receives fresh Bilibili credentials");
		auto *settings = obs_service_get_settings(current);
		check(!std::strstr(obs_data_get_json(settings), "direct-fixture-key"),
		      "direct credentials are not serializable");
		obs_data_release(settings);
		direct.restore();
		check(current == replacement, "direct stop restores previous OBS profile service");
	}
	const auto routes = Core::parseObsStreamRoutes(
		R"({"services":[{"name":"Unrelated","servers":[{"url":"rtmp://ignore/live"}]},{"name":"Bilibili Live - RTMP","servers":[{"name":"API","url":"rtmp://api.example/live"},{"name":"Overseas","url":"rtmp://overseas.example/live"},{"name":"Invalid","url":"https://wrong.example"}]}]})");
	check(routes.size() == 2, "load only valid OBS Bilibili stream routes");
	check(Core::preferredStreamRoute(routes, routes[0].url, false) == 0,
	      "unknown direct latency falls back to the API-returned address");
	auto measured = routes;
	measured[0].latencyMs = 200;
	measured[1].latencyMs = 20;
	check(Core::preferredStreamRoute(measured, routes[0].url, false) == 1 &&
		      Core::preferredStreamRoute(measured, routes[0].url, true) == 0,
	      "direct prefers measured connection; proxy preserves API-returned address");
	QTcpServer receiver;
	receiver.setProxy(QNetworkProxy::NoProxy);
	check(receiver.listen(QHostAddress::LocalHost), "bind direct route probe fixture");
	bool probed = false;
	Core::measureDirectRoutes(
		&app, {{"fixture", "rtmp://127.0.0.1:" + QString::number(receiver.serverPort()) + "/live", -1}},
		[&](Core::StreamRoutes result) { probed = result.size() == 1 && result[0].latencyMs >= 0; }, 500);
	QElapsedTimer wait;
	wait.start();
	while (!probed && wait.elapsed() < 1000)
		QCoreApplication::processEvents();
	check(probed, "measure direct TCP route without RTMP publish");
	check(Core::applyObsStreamRoute("rtmp://selected.example/live", "fixture-stream-key", error),
	      "one-click replaces OBS configuration");
	auto *selectedSettings = obs_service_get_settings(current);
	check(std::strcmp(obs_data_get_string(selectedSettings, "server"), "rtmp://selected.example/live") == 0 &&
		      std::strcmp(obs_data_get_string(selectedSettings, "key"), "fixture-stream-key") == 0,
	      "OBS receives selected address and current stream key");
	obs_data_release(selectedSettings);
	active = true;
	check(!Core::applyObsStreamRoute("rtmp://selected.example/live", "fixture-stream-key", error),
	      "active output cannot be overwritten");
	active = false;
	{
		Core::ConfigManager before(dir.path().toStdString());
		check(!before.config().proxy_unlocked, "new public settings begin with hidden proxy entry");
		before.config().sync_obs_start = false;
		before.config().sync_obs_stop = true;
		before.config().proxy_unlocked = true;
		before.config().cookies = "bili_jct=fixture-csrf; SESSDATA=fixture-cookie";
		before.config().last_rtmp_address = "rtmp://fixture.example/live";
		before.config().last_rtmp_key = "fixture-stream-secret";
		before.config().selected_rtmp_address = "rtmp://selected.example/live";
		check(before.save(), "save state before simulated plugin update");
		QFile disk(dir.filePath("config.json"));
		disk.open(QIODevice::ReadOnly);
		const auto stored = disk.readAll();
		check(!stored.contains("fixture-stream-secret") && !stored.contains("fixture-cookie") &&
			      !stored.contains("rtmp://fixture"),
		      "stream history and login use encrypted storage");
		Core::ConfigManager after(dir.path().toStdString());
		check(after.load() && after.config().proxy_unlocked && !after.config().sync_obs_start &&
			      after.config().sync_obs_stop &&
			      after.config().last_rtmp_key == before.config().last_rtmp_key &&
			      after.config().cookies == before.config().cookies &&
			      after.config().selected_rtmp_address == before.config().selected_rtmp_address,
		      "plugin reload/update preserves unlock, independent switches, login and selected stream route");
	}
	obs_frontend_set_streaming_service(nullptr);
	obs_service_release(original);
	obs_service_release(replacement);
	obs_shutdown();
	std::cout << "OBS service tests: " << failures << " failure(s)\n";
	return failures ? 1 : 0;
}
