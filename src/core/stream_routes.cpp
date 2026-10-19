#include "stream_routes.hpp"
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxy>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <memory>
#include <obs-module.h>
#include <obs-frontend-api.h>

namespace Core {
StreamRoutes parseObsStreamRoutes(const QByteArray &json)
{
	StreamRoutes routes;
	const auto services = QJsonDocument::fromJson(json).object()["services"].toArray();
	for (const auto &entry : services) {
		const auto service = entry.toObject();
		if (!service["name"].toString().startsWith("Bilibili Live"))
			continue;
		for (const auto &item : service["servers"].toArray()) {
			const auto server = item.toObject();
			const QUrl url(server["url"].toString(), QUrl::StrictMode);
			if (!url.isValid() || url.host().isEmpty() ||
			    (url.scheme() != "rtmp" && url.scheme() != "rtmps") || !url.userInfo().isEmpty())
				continue;
			bool duplicate = false;
			for (const auto &route : routes)
				duplicate |= route.url == url.toString();
			if (!duplicate)
				routes.push_back({server["name"].toString(), url.toString(), -1});
		}
	}
	return routes;
}

StreamRoutes obsStreamRoutes()
{
	auto *module = obs_get_module("rtmp-services");
	if (!module)
		return {};
	// OBS's downloaded service catalog takes precedence over its installed catalog.
	char *updated = obs_module_get_config_path(module, "services.json");
	char *bundled = obs_find_module_file(module, "services.json");
	StreamRoutes routes;
	for (const auto *path : {updated, bundled}) {
		if (!path)
			continue;
		QFile file(QString::fromUtf8(path));
		if (file.open(QIODevice::ReadOnly) && file.size() <= 8 * 1024 * 1024)
			routes = parseObsStreamRoutes(file.readAll());
		if (!routes.isEmpty())
			break;
	}
	bfree(updated);
	bfree(bundled);
	return routes;
}

int preferredStreamRoute(const StreamRoutes &routes, const QString &apiAddress, bool streamProxy)
{
	int api = -1, best = -1;
	for (int i = 0; i < routes.size(); ++i) {
		if (routes[i].url == apiAddress)
			api = i;
		if (routes[i].latencyMs >= 0 && (best < 0 || routes[i].latencyMs < routes[best].latencyMs))
			best = i;
	}
	return streamProxy || best < 0 ? api : best;
}

void measureDirectRoutes(QObject *context, StreamRoutes routes, std::function<void(StreamRoutes)> complete,
			 int timeoutMs)
{
	// Probe TCP connectivity only. No RTMP publish, stream key or system proxy is used.
	auto *job = new QObject(context);
	struct State {
		StreamRoutes routes;
		int remaining = 0;
		bool finished = false;
	};
	auto state = std::make_shared<State>();
	state->routes = std::move(routes);
	state->remaining = state->routes.size();
	auto finish = [job, state, complete = std::move(complete)] {
		if (state->finished)
			return;
		state->finished = true;
		complete(std::move(state->routes));
		job->deleteLater();
	};
	QTimer::singleShot(timeoutMs, job, finish);
	if (!state->remaining) {
		QTimer::singleShot(0, job, finish);
		return;
	}
	for (int i = 0; i < state->routes.size(); ++i) {
		auto *socket = new QTcpSocket(job);
		socket->setProxy(QNetworkProxy::NoProxy);
		auto elapsed = std::make_shared<QElapsedTimer>();
		elapsed->start();
		auto settled = std::make_shared<bool>(false);
		auto settle = [socket, state, elapsed, settled, finish, i](bool connected) {
			if (state->finished || *settled)
				return;
			*settled = true;
			if (connected)
				state->routes[i].latencyMs = elapsed->elapsed();
			socket->abort();
			if (--state->remaining == 0)
				finish();
		};
		QObject::connect(socket, &QTcpSocket::connected, job, [settle] { settle(true); });
		QObject::connect(socket, &QTcpSocket::errorOccurred, job,
				 [settle](QAbstractSocket::SocketError) { settle(false); });
		const QUrl url(state->routes[i].url);
		socket->connectToHost(url.host(), quint16(url.port(url.scheme() == "rtmps" ? 443 : 1935)));
	}
}

bool applyObsStreamRoute(const QString &address, const std::string &key, QString &error)
{
	if (obs_frontend_streaming_active()) {
		error = QStringLiteral("请先停止 OBS 推流，再替换推流配置。");
		return false;
	}
	const QUrl url(address, QUrl::StrictMode);
	if (!url.isValid() || url.host().isEmpty() || (url.scheme() != "rtmp" && url.scheme() != "rtmps") ||
	    !url.userInfo().isEmpty() || key.empty()) {
		error = QStringLiteral("推流地址或推流码无效，请先成功开播获取推流码。");
		return false;
	}
	auto *settings = obs_data_create();
	obs_data_set_string(settings, "server", address.toUtf8().constData());
	obs_data_set_string(settings, "key", key.c_str());
	obs_data_set_bool(settings, "use_auth", false);
	auto *service = obs_service_create("rtmp_custom", "Bilibili selected route", settings, nullptr);
	obs_data_release(settings);
	if (!service) {
		error = QStringLiteral("OBS 自定义 RTMP 服务不可用。");
		return false;
	}
	obs_frontend_set_streaming_service(service);
	obs_frontend_save_streaming_service();
	obs_service_release(service);
	return true;
}
} // namespace Core
