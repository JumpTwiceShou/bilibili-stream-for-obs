#pragma once
#include "proxy_runtime.hpp"
#include <QTcpServer>
#include <QUrl>

namespace Proxy {
class StreamRelay : public QObject {
	Q_OBJECT
public:
	explicit StreamRelay(QObject *parent = nullptr);
	~StreamRelay() override { stop(); }
	bool start(const QString &upstream, const Endpoint &endpoint, QString &error);
	void stop();
	QString localUrl() const { return m_localUrl; }
	bool ready() const { return m_server.isListening() && m_endpoint.ready(); }
signals:
	void failed(QString message);

private:
	QTcpServer m_server;
	QTimer m_health;
	QUrl m_upstream;
	Endpoint m_endpoint;
	QString m_localUrl;
	QList<QObject *> m_sessions;
};
} // namespace Proxy
