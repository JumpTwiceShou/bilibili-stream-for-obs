#include "stream_relay.hpp"
#include "rtmp_rewriter.hpp"
#include <QSslSocket>
#include <QPointer>
#include <functional>

namespace Proxy {
namespace {
class Session : public QObject {
public:
	Session(QTcpSocket *client, const QUrl &target, Endpoint endpoint, QObject *parent,
		std::function<void(QString)> failure)
		: QObject(parent),
		  local(client),
		  remote(new QSslSocket(this)),
		  endpoint(std::move(endpoint)),
		  rewriter(target.toEncoded()),
		  failure(std::move(failure))
	{
		local->setParent(this);
		local->setReadBufferSize(limit);
		remote->setReadBufferSize(limit);
		remote->setProxy(this->endpoint.networkProxy());
		timeout.setParent(this);
		timeout.setSingleShot(true);
		connect(&timeout, &QTimer::timeout, this, [this] { close(QStringLiteral("推流代理连接超时。")); });
		connect(local, &QTcpSocket::readyRead, this, [this] { upload(); });
		connect(remote, &QTcpSocket::readyRead, this, [this] { download(); });
		connect(local, &QTcpSocket::bytesWritten, this, [this](qint64) { download(); });
		connect(remote, &QTcpSocket::bytesWritten, this, [this](qint64 bytes) {
			this->endpoint.counters->uploaded.fetch_add(quint64(bytes));
			upload();
		});
		connect(local, &QTcpSocket::disconnected, this, [this] { close({}); });
		connect(remote, &QTcpSocket::disconnected, this,
			[this] { close(QStringLiteral("推流代理远端连接已断开。")); });
		connect(remote, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
			close(QStringLiteral("推流代理连接失败，请检测节点或检查推流地址。"));
		});
		auto connected = [this] {
			connectedToTarget = true;
			timeout.stop();
			upload();
		};
		if (target.scheme() == "rtmps") {
			connect(remote, &QSslSocket::encrypted, this, connected);
			connect(remote, &QSslSocket::sslErrors, this, [this](const QList<QSslError> &) {
				close(QStringLiteral("RTMPS 服务器证书验证失败。"));
			});
			remote->connectToHostEncrypted(target.host(), quint16(target.port(443)));
		} else {
			connect(remote, &QTcpSocket::connected, this, connected);
			remote->connectToHost(target.host(), quint16(target.port(1935)));
		}
		timeout.start(15000);
	}

private:
	void upload()
	{
		if (closing || !connectedToTarget)
			return;
		if (!endpoint.ready()) {
			close(QStringLiteral("代理核心不可用。"));
			return;
		}
		while (local->bytesAvailable() && remote->bytesToWrite() < limit) {
			QByteArray output;
			QString error;
			if (!rewriter.feed(local->read(64 * 1024), output, error)) {
				close(error);
				return;
			}
			if (!output.isEmpty() && remote->write(output) != output.size()) {
				close(QStringLiteral("推流代理发送失败。"));
				return;
			}
		}
	}
	void download()
	{
		if (closing)
			return;
		while (remote->bytesAvailable() && local->bytesToWrite() < limit) {
			const auto data = remote->read(64 * 1024);
			endpoint.counters->downloaded.fetch_add(quint64(data.size()));
			if (local->write(data) != data.size()) {
				close(QStringLiteral("本地推流连接已断开。"));
				return;
			}
		}
	}
	void close(const QString &error)
	{
		if (closing)
			return;
		closing = true;
		timeout.stop();
		local->abort();
		remote->abort();
		if (!error.isEmpty())
			failure(error);
		deleteLater();
	}
	static constexpr qint64 limit = 1024 * 1024;
	QTcpSocket *local;
	QSslSocket *remote;
	Endpoint endpoint;
	RtmpRewriter rewriter;
	std::function<void(QString)> failure;
	QTimer timeout;
	bool connectedToTarget = false, closing = false;
};
} // namespace

StreamRelay::StreamRelay(QObject *parent) : QObject(parent)
{
	m_server.setProxy(QNetworkProxy::NoProxy);
	connect(&m_server, &QTcpServer::newConnection, this, [this] {
		while (m_server.hasPendingConnections()) {
			auto *client = m_server.nextPendingConnection();
			if (!ready() || !m_sessions.isEmpty()) {
				client->abort();
				client->deleteLater();
				continue;
			}
			auto *session = new Session(client, m_upstream, m_endpoint, this,
						    [this](QString error) { emit failed(error); });
			m_sessions.append(session);
			connect(session, &QObject::destroyed, this, [this, session] { m_sessions.removeAll(session); });
		}
	});
	m_health.setInterval(200);
	connect(&m_health, &QTimer::timeout, this, [this] {
		if (!m_endpoint.ready()) {
			stop();
			emit failed(QStringLiteral("代理核心已停止；推流连接已关闭。"));
		}
	});
}
bool StreamRelay::start(const QString &upstream, const Endpoint &endpoint, QString &error)
{
	stop();
	m_upstream = QUrl(upstream, QUrl::StrictMode);
	if (!endpoint.ready()) {
		error = QStringLiteral("代理核心尚未就绪，请在代理设置中检查状态。");
		return false;
	}
	if (!m_upstream.isValid() || (m_upstream.scheme() != "rtmp" && m_upstream.scheme() != "rtmps") ||
	    m_upstream.host().isEmpty() || !m_upstream.userInfo().isEmpty() || m_upstream.hasFragment()) {
		error = QStringLiteral("代理推流仅支持有效的 RTMP/RTMPS 地址。");
		return false;
	}
	m_endpoint = endpoint;
	if (!m_server.listen(QHostAddress::LocalHost, 0)) {
		error = QStringLiteral("无法监听本地推流端口。");
		return false;
	}
	QUrl local = m_upstream;
	local.setScheme("rtmp");
	local.setHost("127.0.0.1");
	local.setPort(m_server.serverPort());
	m_localUrl = local.toString(QUrl::FullyEncoded);
	m_health.start();
	return true;
}
void StreamRelay::stop()
{
	m_health.stop();
	m_server.close();
	const auto sessions = m_sessions;
	for (auto *session : sessions)
		delete session;
	m_sessions.clear();
	m_localUrl.clear();
}
} // namespace Proxy
