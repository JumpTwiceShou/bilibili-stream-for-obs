#pragma once
#include "proxy_settings.hpp"
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QElapsedTimer>
#include <QTcpSocket>
#include <QNetworkProxy>
#include <atomic>
#include <memory>

namespace Proxy {
struct Counters {
	std::atomic_ullong uploaded{0}, downloaded{0};
};
struct Endpoint {
	quint16 port = 0;
	QString user, password;
	std::shared_ptr<std::atomic_bool> alive = std::make_shared<std::atomic_bool>(false);
	std::shared_ptr<Counters> counters = std::make_shared<Counters>();
	bool ready() const { return port && alive->load(); }
	QNetworkProxy networkProxy() const
	{
		return QNetworkProxy(QNetworkProxy::Socks5Proxy, "127.0.0.1", port, user, password);
	}
};
class Runtime : public QObject {
	Q_OBJECT
public:
	explicit Runtime(QString executable, QObject *parent = nullptr);
	~Runtime() override;
	void start(const QString &uri);
	void start(const Settings &settings);
	void stop();
	Endpoint endpoint() const { return m_endpoint; }
	QString status() const { return m_status; }
	bool starting() const { return m_starting; }
	bool hasFailure() const { return m_failed; }
	void setAutoReconnect(bool enabled) { m_autoReconnect = enabled; }
signals:
	void changed();

private:
	void fail(const QString &message, bool retryAllowed = true);
	void launch(const Settings &settings);
	Settings m_settings;
	QTimer m_reconnect;
	QElapsedTimer m_connectedFor;
	int m_retryCount = 0, m_statusFailures = 0;
	bool m_autoReconnect = false;
	bool m_failed = false;
	QString m_executable, m_status = QStringLiteral("代理已关闭");
	QProcess m_process;
	QProcess m_vpnStatus;
	QTimer m_vpnPoll, m_statusDeadline;
	QTimer m_poll, m_deadline;
	QTcpSocket m_probe;
	Endpoint m_endpoint;
	QByteArray m_probeData;
	int m_authStage = 0;
	bool m_starting = false, m_stopping = false;
	bool m_openVpn = false;
	void *m_job = nullptr;
};
} // namespace Proxy
