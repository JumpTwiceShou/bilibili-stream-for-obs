#pragma once
#include <QByteArray>
#include <QString>
#include <QVector>
#include <QObject>
#include <functional>
#include <string>

namespace Core {
struct StreamRoute {
	QString name, url;
	qint64 latencyMs = -1;
};
using StreamRoutes = QVector<StreamRoute>;
StreamRoutes parseObsStreamRoutes(const QByteArray &json);
StreamRoutes obsStreamRoutes();
int preferredStreamRoute(const StreamRoutes &routes, const QString &apiAddress, bool streamProxy);
void measureDirectRoutes(QObject *context, StreamRoutes routes, std::function<void(StreamRoutes)> complete,
			 int timeoutMs = 2500);
bool applyObsStreamRoute(const QString &address, const std::string &key, QString &error);
} // namespace Core
