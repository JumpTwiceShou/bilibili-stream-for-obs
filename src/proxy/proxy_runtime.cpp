#include "proxy_runtime.hpp"
#include "openvpn_status.hpp"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QTcpServer>
#include <QUuid>
#ifdef _WIN32
#include <windows.h>
#endif

namespace Proxy {
Runtime::Runtime(QString executable, QObject *parent) : QObject(parent), m_executable(std::move(executable))
{
	m_reconnect.setSingleShot(true);
	connect(&m_reconnect, &QTimer::timeout, this, [this] { launch(m_settings); });
	m_poll.setInterval(100);
	m_deadline.setSingleShot(true);
	m_vpnPoll.setInterval(500);
	m_statusDeadline.setSingleShot(true);
	connect(&m_vpnPoll, &QTimer::timeout, this, [this] {
		if (m_vpnStatus.state() != QProcess::NotRunning)
			return;
		m_vpnStatus.start(m_executable, {"api", "openvpn", "status"});
		m_statusDeadline.start(2500);
	});
	connect(&m_statusDeadline, &QTimer::timeout, this, [this] { m_vpnStatus.kill(); });
	connect(&m_vpnStatus, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
		// finished handles transient command failures; FailedToStart has no finished signal.
		if (!m_stopping && m_vpnStatus.error() == QProcess::FailedToStart && ++m_statusFailures >= 3)
			fail(QStringLiteral("OpenVPN 状态检测不可用；已阻止直连。"));
	});
	connect(&m_vpnStatus, &QProcess::started, this, [this] {
#ifdef _WIN32
		HANDLE child =
			OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, DWORD(m_vpnStatus.processId()));
		const bool bound = child && m_job && AssignProcessToJobObject(HANDLE(m_job), child);
		if (child)
			CloseHandle(child);
		if (!bound)
			fail(QStringLiteral("无法绑定 VPN 状态检测进程，已停止代理。"));
#endif
	});
	connect(&m_vpnStatus, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
		[this](int code, QProcess::ExitStatus exitStatus) {
			m_statusDeadline.stop();
			const auto output = m_vpnStatus.readAllStandardOutput();
			m_vpnStatus.readAllStandardError();
			if (m_stopping)
				return;
			// CLI and its output format are pinned with the core's SHA256. Never display raw server status.
			const auto status = parseOpenVpnStatus(output);
			const QString state = status.state;
			if (code == 0 && exitStatus == QProcess::NormalExit && status.authenticationRejected) {
				fail(QStringLiteral(
					     "OpenVPN 认证被服务器拒绝；已暂停自动重试，请核对 VPN 专用账号、密码及服务状态。"),
				     false);
				return;
			}
			if (code == 0 && exitStatus == QProcess::NormalExit && status.certificateRejected) {
				fail(QStringLiteral("OpenVPN 证书校验失败；已暂停自动重试，请核对配置及系统时间。"),
				     false);
				return;
			}
			if (code == 0 && exitStatus == QProcess::NormalExit && state == "connected") {
				m_failed = false;
				m_statusFailures = 0;
				if (m_connectedFor.isValid() && m_connectedFor.elapsed() > 60000)
					m_retryCount = 0;
				if (!m_endpoint.ready()) {
					m_deadline.stop();
					m_endpoint.alive->store(true);
					m_starting = false;
					m_connectedFor.start();
					m_status = QStringLiteral("OpenVPN 隧道已连接");
					m_vpnPoll.setInterval(2000);
					emit changed();
				}
			} else {
				// A rekey/temporary status timeout must not kill an otherwise recovering tunnel.
				if (m_endpoint.ready()) {
					m_endpoint.alive->store(false);
					m_starting = true;
					m_connectedFor.invalidate();
					m_status = QStringLiteral("OpenVPN 暂时断线，正在恢复连接…");
					m_deadline.start(30000);
					emit changed();
				}
				if (code == 0 && exitStatus == QProcess::NormalExit &&
				    (state == "connecting" || state == "auth-pending")) {
					m_statusFailures = 0;
				} else if (++m_statusFailures >= 3)
					fail(QStringLiteral("OpenVPN 连接中断；已阻止直连。"));
			}
		});
	m_process.setProcessChannelMode(QProcess::SeparateChannels);
	connect(&m_process, &QProcess::readyReadStandardError, this, [this] { m_process.readAllStandardError(); });
	connect(&m_process, &QProcess::readyReadStandardOutput, this, [this] { m_process.readAllStandardOutput(); });
	connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
		if (!m_stopping)
			fail(QStringLiteral("代理核心无法启动，请检查插件安装包是否完整。"));
	});
	connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
		[this](int, QProcess::ExitStatus) {
			if (!m_stopping)
				fail(QStringLiteral("代理核心已退出；已阻止此代理范围直连。"));
		});
	connect(&m_process, &QProcess::started, this, [this] {
#ifdef _WIN32
		m_job = CreateJobObjectW(nullptr, nullptr);
		JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
		limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
		HANDLE child = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, DWORD(m_process.processId()));
		const bool bound = m_job && child &&
				   SetInformationJobObject(HANDLE(m_job), JobObjectExtendedLimitInformation, &limits,
							   sizeof(limits)) &&
				   AssignProcessToJobObject(HANDLE(m_job), child);
		if (child)
			CloseHandle(child);
		if (!bound) {
			fail(QStringLiteral("无法绑定代理核心生命周期，已停止核心。"));
			return;
		}
#endif
		m_poll.start();
	});
	connect(&m_poll, &QTimer::timeout, this, [this] {
		if (m_probe.state() != QAbstractSocket::UnconnectedState)
			return;
		m_authStage = 0;
		m_probeData.clear();
		m_probe.setProxy(QNetworkProxy::NoProxy);
		m_probe.connectToHost(QHostAddress::LocalHost, m_endpoint.port);
	});
	connect(&m_probe, &QTcpSocket::connected, this, [this] { m_probe.write(QByteArray::fromHex("050102")); });
	connect(&m_probe, &QTcpSocket::readyRead, this, [this] {
		m_probeData += m_probe.readAll();
		if (m_probeData.size() < 2)
			return;
		const QByteArray reply = m_probeData.left(2);
		m_probeData.remove(0, 2);
		if (m_authStage == 0 && reply == QByteArray::fromHex("0502")) {
			const auto user = m_endpoint.user.toUtf8(), password = m_endpoint.password.toUtf8();
			QByteArray auth(1, char(1));
			auth += char(user.size());
			auth += user;
			auth += char(password.size());
			auth += password;
			m_authStage = 1;
			m_probe.write(auth);
			return;
		}
		if (m_authStage == 1 && reply == QByteArray::fromHex("0100")) {
			m_poll.stop();
			m_probe.abort();
			if (m_openVpn) {
				m_status = QStringLiteral("正在等待 OpenVPN 握手和认证…");
				m_vpnPoll.start();
				emit changed();
				return;
			}
			m_deadline.stop();
			m_endpoint.alive->store(true);
			m_starting = false;
			m_status = QStringLiteral("SS 核心就绪（节点连通性请点击检测）");
			emit changed();
			return;
		}
		fail(QStringLiteral("本地代理认证失败。"));
	});
	connect(&m_deadline, &QTimer::timeout, this, [this] { fail(QStringLiteral("代理核心启动超时。")); });
}

Runtime::~Runtime()
{
	stop();
}
void Runtime::stop()
{
	m_reconnect.stop();
	m_stopping = true;
	m_endpoint.alive->store(false);
	m_starting = false;
	m_failed = false;
	m_poll.stop();
	m_deadline.stop();
	m_probe.abort();
	m_vpnPoll.stop();
	m_statusDeadline.stop();
	if (m_vpnStatus.state() != QProcess::NotRunning) {
		m_vpnStatus.kill();
		m_vpnStatus.waitForFinished(1500);
	}
	if (m_process.state() != QProcess::NotRunning) {
		m_process.kill();
		m_process.waitForFinished(1500);
	}
#ifdef _WIN32
	if (m_job) {
		CloseHandle(HANDLE(m_job));
		m_job = nullptr;
	}
#endif
	m_stopping = false;
	m_status = QStringLiteral("代理已关闭");
	emit changed();
}
void Runtime::fail(const QString &message, bool retryAllowed)
{
	stop();
	m_status = message;
	m_failed = true;
	if (retryAllowed && m_autoReconnect && m_settings.enabled && m_retryCount < 5) {
		m_reconnect.start(1000 * (1 << m_retryCount++));
		m_starting = true;
		m_status = QStringLiteral("连接断开，正在自动重连（%1/5）…").arg(m_retryCount);
	}
	emit changed();
}

void Runtime::start(const QString &uri)
{
	Settings settings;
	settings.uri = uri;
	start(settings);
}
void Runtime::start(const Settings &settings)
{
	m_settings = settings;
	m_retryCount = 0;
	launch(settings);
}
void Runtime::launch(const Settings &settings)
{
	stop();
	m_statusFailures = 0;
	m_connectedFor.invalidate();
	QString error;
	if (!validateNode(settings, error)) {
		fail(error);
		return;
	}
	QFile executable(m_executable);
	QCryptographicHash hash(QCryptographicHash::Sha256);
	if (!executable.open(QIODevice::ReadOnly) || !hash.addData(&executable) ||
	    hash.result().toHex() != "5c19bb3f8c12d4e85b3d47fe63123a7624b314b58dd7a8e32f51004f1c4d2faf") {
		fail(QStringLiteral("代理核心缺失或校验失败，请重新解压插件安装包。"));
		return;
	}
	executable.close();
	QTcpServer reserve;
	reserve.setProxy(QNetworkProxy::NoProxy);
	if (!reserve.listen(QHostAddress::LocalHost, 0)) {
		fail(QStringLiteral("无法分配本地代理端口。"));
		return;
	}
	m_endpoint = {};
	m_endpoint.port = reserve.serverPort();
	m_endpoint.user = QUuid::createUuid().toString(QUuid::Id128);
	m_endpoint.password = QUuid::createUuid().toString(QUuid::Id128);
	m_openVpn = settings.backend == "openvpn";
	auto configObject = coreConfig(settings, m_endpoint.port, m_endpoint.user, m_endpoint.password, error);
	if (m_openVpn) {
		QTcpServer apiReserve;
		apiReserve.setProxy(QNetworkProxy::NoProxy);
		if (!apiReserve.listen(QHostAddress::LocalHost, 0)) {
			fail(QStringLiteral("无法分配 VPN 状态端口。"));
			return;
		}
		const int apiPort = apiReserve.serverPort();
		const QString secret = QUuid::createUuid().toString(QUuid::Id128);
		configObject["services"] =
			QJsonArray{QJsonObject{{"type", "api"},
					       {"listen", "127.0.0.1"},
					       {"listen_port", apiPort},
					       {"secret", secret},
					       {"dashboard", false},
					       {"access_control_allow_origin", QJsonArray{"http://127.0.0.1"}}}};
		auto environment = QProcessEnvironment::systemEnvironment();
		environment.insert("BOX_API_URL", "http://127.0.0.1:" + QString::number(apiPort));
		environment.insert("BOX_API_SECRET", secret);
		environment.insert("NO_PROXY", "127.0.0.1,localhost");
		environment.insert("no_proxy", "127.0.0.1,localhost");
		m_vpnStatus.setProcessEnvironment(environment);
		m_vpnPoll.setInterval(500);
	}
	reserve.close();
	m_status = QStringLiteral("正在启动代理…");
	m_starting = true;
	m_process.start(m_executable, {"run", "-c", "stdin"});
	const auto config = QJsonDocument(configObject).toJson(QJsonDocument::Compact);
	m_process.write(config);
	m_process.closeWriteChannel();
	m_deadline.start(m_openVpn ? 60000 : 8000);
	emit changed();
}
} // namespace Proxy
