#include "proxy_dialog.hpp"
#include "toggle_switch.hpp"
#include "http_client.hpp"
#include <curl/curl.h>
#include <QCoreApplication>
#include <QPointer>
#include <QSignalBlocker>
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFile>
#include <QFocusEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QScrollArea>
#include <QScreen>
#include <QGuiApplication>
#include <algorithm>
#include <memory>

namespace UI {
namespace {
class CredentialEdit : public QLineEdit {
public:
	using QLineEdit::QLineEdit;
	bool losingWindowFocus() const { return m_losingWindowFocus; }

protected:
	void focusOutEvent(QFocusEvent *event) override
	{
		m_losingWindowFocus = event->reason() == Qt::ActiveWindowFocusReason;
		QLineEdit::focusOutEvent(event);
		m_losingWindowFocus = false;
	}

private:
	bool m_losingWindowFocus = false;
};
// Page scrolling must never silently select and save a different proxy backend.
class SettingsComboBox : public QComboBox {
protected:
	void wheelEvent(QWheelEvent *event) override { event->ignore(); }
};
class SettingsDialog : public QDialog {
public:
	using QDialog::QDialog;
	std::function<bool()> canClose;
	void done(int result) override
	{
		if (!canClose || canClose())
			QDialog::done(result);
	}
};
QString bytes(quint64 value)
{
	if (value >= 1024 * 1024 * 1024)
		return QString::number(double(value) / (1024 * 1024 * 1024), 'f', 2) + " GiB";
	if (value >= 1024 * 1024)
		return QString::number(double(value) / (1024 * 1024), 'f', 2) + " MiB";
	return QString::number(double(value) / 1024, 'f', 1) + " KiB";
}
} // namespace
void proxyDialog(QWidget *parent, const Proxy::Settings &current, Proxy::Runtime &runtime, const QString &corePath,
		 std::function<bool()> locked, std::function<bool(const Proxy::Settings &, QString &)> apply)
{
	SettingsDialog dialog(parent);
	dialog.setWindowTitle(QStringLiteral("VPN / 代理设置"));
	dialog.setMinimumWidth(560);
	auto *outer = new QVBoxLayout(&dialog);
	outer->setContentsMargins(16, 16, 16, 12);
	outer->setSpacing(12);
	auto *lockNotice = new QLabel;
	lockNotice->setObjectName("proxyLockNotice");
	lockNotice->setWordWrap(true);
	outer->addWidget(lockNotice);
	auto *scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	auto *content = new QWidget;
	auto *layout = new QVBoxLayout(content);
	layout->setContentsMargins(0, 0, 8, 0);
	layout->setSpacing(14);
	scroll->setWidget(content);
	outer->addWidget(scroll);
	const auto *screen = parent ? parent->screen() : QGuiApplication::primaryScreen();
	const int available = screen ? screen->availableGeometry().height() : 900;
	dialog.resize(680, qBound(380, available - 100, 820));
	auto *enabled = new ToggleSwitch(QStringLiteral("启用 VPN / 代理"));
	enabled->setObjectName("proxyEnabled");
	enabled->setChecked(current.enabled);
	layout->addWidget(enabled);
	auto *scope = new QGroupBox(QStringLiteral("代理范围"));
	auto *scopeLayout = new QVBoxLayout(scope);
	scopeLayout->setSpacing(6);
	auto *api = new ToggleSwitch(QStringLiteral("B 站接口"));
	api->setObjectName("proxyApi");
	api->setChecked(current.api);
	auto *stream = new ToggleSwitch(QStringLiteral("OBS 视频推流"));
	stream->setObjectName("proxyStream");
	stream->setChecked(current.stream);
	scopeLayout->addWidget(api);
	auto *apiNote = new QLabel(QStringLiteral("登录、开播、停播和直播间设置。仅开启此项时，视频仍可直连。"));
	apiNote->setWordWrap(true);
	api->setAccessibleDescription(apiNote->text());
	scopeLayout->addWidget(apiNote);
	scopeLayout->addWidget(stream);
	auto *streamNote = new QLabel(QStringLiteral("让本插件准备的直播视频也经过代理，需全程保持连接。"));
	streamNote->setWordWrap(true);
	stream->setAccessibleDescription(streamNote->text());
	scopeLayout->addWidget(streamNote);
	layout->addWidget(scope);
	auto *connectionDivider = new QFrame;
	connectionDivider->setFrameShape(QFrame::HLine);
	scopeLayout->addWidget(connectionDivider);
	auto *keep = new ToggleSwitch(QStringLiteral("接口模式保持连接"));
	keep->setObjectName("keepConnected");
	keep->setChecked(current.keepConnected);
	scopeLayout->addWidget(keep);
	auto *keepNote = new QLabel;
	keepNote->setObjectName("proxyKeepNote");
	keepNote->setWordWrap(true);
	scopeLayout->addWidget(keepNote);
	auto *nodeGroup = new QGroupBox(QStringLiteral("节点配置"));
	auto *nodeLayout = new QVBoxLayout(nodeGroup);
	nodeLayout->setSpacing(8);
	layout->addWidget(nodeGroup);
	auto *backend = new SettingsComboBox;
	backend->setFocusPolicy(Qt::StrongFocus);
	backend->setObjectName("proxyBackend");
	backend->addItem(QStringLiteral("Shadowsocks（SS 链接）"), "ss");
	backend->addItem(QStringLiteral("OpenVPN（导入配置）"), "openvpn");
	backend->setCurrentIndex(current.backend == "openvpn" ? 1 : 0);
	auto *backendForm = new QFormLayout;
	backendForm->addRow(QStringLiteral("连接方式"), backend);
	nodeLayout->addLayout(backendForm);
	auto *ssGroup = new QWidget;
	auto *ssLayout = new QVBoxLayout(ssGroup);
	ssLayout->setContentsMargins(0, 0, 0, 0);
	auto *form = new QFormLayout;
	auto *uri = new CredentialEdit(current.uri);
	uri->setEchoMode(QLineEdit::Password);
	uri->setMaxLength(8192);
	uri->setObjectName("ssUri");
	uri->setPlaceholderText(QStringLiteral("粘贴服务商提供的 ss:// 链接"));
	form->addRow(QStringLiteral("SS 链接"), uri);
	ssLayout->addLayout(form);
	auto *show = new ToggleSwitch(QStringLiteral("显示链接"));
	show->setObjectName("showSsUri");
	ssLayout->addWidget(show);
	nodeLayout->addWidget(ssGroup);
	QObject::connect(show, &QCheckBox::toggled, &dialog,
			 [uri](bool visible) { uri->setEchoMode(visible ? QLineEdit::Normal : QLineEdit::Password); });
	auto *vpnGroup = new QWidget;
	auto *vpnLayout = new QVBoxLayout(vpnGroup);
	vpnLayout->setContentsMargins(0, 0, 0, 0);
	auto *presets = new SettingsComboBox;
	presets->setFocusPolicy(Qt::StrongFocus);
	presets->setObjectName("vpnPreset");
	presets->setAccessibleName(QStringLiteral("OpenVPN 配置"));
	presets->addItem(QStringLiteral("手动导入的配置"), QString());
#ifdef PERSONAL_OVPN_BUNDLED
	for (const auto &transport : {QString("tcp"), QString("udp")}) {
		QFile bundled(":/personal-vpn/" + transport + ".ovpn");
		if (bundled.open(QIODevice::ReadOnly))
			presets->addItem(QStringLiteral("个人专属 · %1（内置）").arg(transport.toUpper()),
					 QString::fromUtf8(bundled.readAll()));
	}
#endif
	auto *profileRow = new QHBoxLayout;
	profileRow->addWidget(presets, 1);
	vpnLayout->addLayout(profileRow);
	auto *import = new QPushButton(QStringLiteral("导入 .ovpn 文件…"));
	import->setAutoDefault(false);
	profileRow->addWidget(import);
	QString profile = current.ovpnProfile;
	int selected = 0;
	for (int i = 1; i < presets->count(); ++i)
		if (presets->itemData(i).toString() == profile)
			selected = i;
	QString customProfile = selected ? QString() : profile;
	if (profile.isEmpty() && presets->count() > 1)
		selected = 1;
	presets->setCurrentIndex(selected);
	if (selected)
		profile = presets->currentData().toString();
	auto *profileStatus = new QLabel;
	profileStatus->setWordWrap(true);
	profileStatus->setTextFormat(Qt::PlainText);
	vpnLayout->addWidget(profileStatus);
	auto profileSummary = [&] {
		QJsonObject endpoint;
		bool requiresPassword = false;
		QString error;
		profileStatus->setText(Proxy::parseOpenVpn(profile, endpoint, requiresPassword, error)
					       ? QStringLiteral("%1 配置 · %2")
							 .arg(endpoint["network"].toString().toUpper(),
							      requiresPassword ? QStringLiteral("需要 VPN 账号密码")
									       : QStringLiteral("证书认证"))
					       : QStringLiteral("请导入服务商提供的 .ovpn 配置文件。"));
	};
	profileSummary();
	QObject::connect(presets, &QComboBox::currentIndexChanged, &dialog, [&](int index) {
		profile = index == 0 ? customProfile : presets->itemData(index).toString();
		profileSummary();
	});
	auto *vpnForm = new QFormLayout;
	auto *username = new CredentialEdit(current.ovpnUsername);
	username->setObjectName("vpnUsername");
	username->setMaxLength(1024);
	username->setPlaceholderText(QStringLiteral("服务商提供的 VPN 用户名"));
	auto *password = new CredentialEdit(current.ovpnPassword);
	password->setObjectName("vpnPassword");
	password->setMaxLength(1024);
	password->setEchoMode(QLineEdit::Password);
	password->setPlaceholderText(QStringLiteral("VPN 密码（加密保存在本机）"));
	vpnForm->addRow(QStringLiteral("用户名"), username);
	vpnForm->addRow(QStringLiteral("密码"), password);
	vpnLayout->addLayout(vpnForm);
	nodeLayout->addWidget(vpnGroup);
	auto *vpnNote = new QLabel(QStringLiteral(
		"支持 TCP / UDP、TUN 及内嵌 CA 配置。外部证书、脚本、TAP、二次验证码和多服务器配置暂不支持。"));
	vpnNote->setWordWrap(true);
	vpnLayout->addWidget(vpnNote);
	auto selectBackend = [&] {
		const bool vpn = backend->currentData() == "openvpn";
		ssGroup->setVisible(!vpn);
		vpnGroup->setVisible(vpn);
	};
	QObject::connect(backend, &QComboBox::currentIndexChanged, &dialog, [&](int) { selectBackend(); });
	selectBackend();
	auto candidate = [&] {
		Proxy::Settings settings{enabled->isChecked(), api->isChecked(), stream->isChecked(),
					 uri->text().trimmed()};
		settings.backend = backend->currentData().toString();
		settings.ovpnProfile = profile;
		settings.ovpnUsername = username->text();
		settings.ovpnPassword = password->text();
		settings.keepConnected = keep->isChecked();
		return settings;
	};
	auto *diagnostics = new QGroupBox(QStringLiteral("连接与检测"));
	auto *diagnosticsLayout = new QVBoxLayout(diagnostics);
	diagnosticsLayout->setSpacing(8);
	layout->addWidget(diagnostics);
	auto *test = new QPushButton(QStringLiteral("检测连接、延迟和出口 IP"));
	test->setObjectName("proxyTest");
	test->setAutoDefault(false);
	auto *result = new QLabel(QStringLiteral("尚未检测。检测会临时连接当前填写的节点。"));
	result->setObjectName("proxyTestResult");
	result->setWordWrap(true);
	result->setTextFormat(Qt::PlainText);
	auto invalidateProbe = [&] {
		result->setText(QStringLiteral("节点已修改，尚未检测。"));
	};
	diagnosticsLayout->addWidget(result);
	auto *saveStatus = new QLabel(QStringLiteral("设置自动保存，无需点击保存。"));
	saveStatus->setObjectName("proxySaveStatus");
	saveStatus->setWordWrap(true);
	saveStatus->setTextFormat(Qt::PlainText);
	std::function<void()> scheduleSave;
	QObject::connect(import, &QPushButton::clicked, &dialog, [&] {
		if (locked())
			return;
		const QString path = QFileDialog::getOpenFileName(&dialog, QStringLiteral("导入 OpenVPN 配置"), {},
								  "OpenVPN (*.ovpn)");
		if (path.isEmpty())
			return;
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly) || file.size() > 256 * 1024) {
			profileStatus->setText(QStringLiteral("导入失败：无法读取配置，或文件大于 256 KiB。"));
			scroll->ensureWidgetVisible(profileStatus);
			return;
		}
		const QString imported = QString::fromUtf8(file.readAll());
		QJsonObject endpoint;
		bool requiresPassword = false;
		QString error;
		if (!Proxy::parseOpenVpn(imported, endpoint, requiresPassword, error)) {
			profileStatus->setText(QStringLiteral("导入失败：") + error);
			scroll->ensureWidgetVisible(profileStatus);
			return;
		}
		customProfile = imported;
		presets->setCurrentIndex(0);
		profile = imported;
		profileSummary();
		profileStatus->setText(profileStatus->text() + QStringLiteral(" · 已导入"));
		invalidateProbe();
		scheduleSave();
	});
	auto *status = new QLabel;
	status->setObjectName("proxyRuntimeStatus");
	status->setWordWrap(true);
	status->setTextFormat(Qt::PlainText);
	diagnosticsLayout->insertWidget(0, status);
	auto *traffic = new QLabel;
	traffic->setWordWrap(true);
	diagnosticsLayout->addWidget(traffic);
	auto *note = new QLabel(QStringLiteral("流量仅统计本插件的代理连接，不含临时检测和网络封装开销。"));
	note->setWordWrap(true);
	diagnosticsLayout->addWidget(note);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
	auto *reconnect = new QPushButton(QStringLiteral("重新连接"));
	reconnect->setObjectName("proxyReconnect");
	reconnect->setAutoDefault(false);
	auto *actions = new QHBoxLayout;
	actions->addWidget(test, 1);
	actions->addWidget(reconnect);
	diagnosticsLayout->insertLayout(1, actions);
	auto *affiliate = new QLabel(QStringLiteral(
		"<a href=\"https://www.vnet.one/r3011477236\">可用 VPN 推荐：VNET（AFF 链接）</a><br>可使用闪连（SS）或任连（OpenVPN），也支持其他兼容服务。此为推广链接，购买后插件作者可能获得返佣。"));
	affiliate->setObjectName("vpnAffiliate");
	affiliate->setOpenExternalLinks(true);
	affiliate->setTextInteractionFlags(Qt::TextBrowserInteraction);
	affiliate->setWordWrap(true);
	layout->addWidget(affiliate);
	layout->addStretch();
	buttons->button(QDialogButtonBox::Close)->setText(QStringLiteral("关闭"));
	buttons->button(QDialogButtonBox::Close)->setAutoDefault(false);
	auto *footer = new QHBoxLayout;
	footer->addWidget(saveStatus, 1);
	footer->addWidget(buttons);
	outer->addLayout(footer);
	Proxy::Runtime tester(corePath);
	QElapsedTimer elapsed, sample;
	sample.start();
	bool testing = false, requested = false;
	bool dirty = false, deferredSave = false;
	bool confirmingClose = false;
	Proxy::Runtime *probeRuntime = nullptr;
	Proxy::Settings activeSettings = current;
	quint64 lastUp = 0, lastDown = 0;
	QTimer refresh;
	std::function<void()> persist;
	auto update = [&] {
		const bool busy = locked();
		lockNotice->setVisible(busy || testing);
		lockNotice->setText(busy ? QStringLiteral("正在直播或执行接口操作，设置已锁定，结束后可修改。")
					 : QStringLiteral("正在检测连接，完成后可修改设置。"));
		if (!busy && !testing && !confirmingClose && deferredSave && persist) {
			deferredSave = false;
			persist();
		}
		keepNote->setText(
			!enabled->isChecked() ? QStringLiteral("代理已关闭。启用后可设置接口模式的连接方式。")
			: stream->isChecked()
				? QStringLiteral("视频推流已开启，代理将全程保持连接。此开关只影响仅接口模式。")
			: !api->isChecked()
				? QStringLiteral("先启用代理和 B 站接口后可设置；关闭此项时仅在接口操作期间临时连接。")
				: QStringLiteral(
					  "关闭：接口操作时临时连接，完成后自动断开。开启：持续连接。停播时若已断开，会自动重连。"));
		keep->setAccessibleDescription(keepNote->text());
		keep->setEnabled(!busy && !testing && enabled->isChecked() && api->isChecked() && !stream->isChecked());
		enabled->setEnabled(!busy && !testing);
		scope->setEnabled(!busy && !testing && enabled->isChecked());
		backend->setEnabled(!busy && !testing);
		ssGroup->setEnabled(!busy && !testing);
		vpnGroup->setEnabled(!busy && !testing);
		test->setEnabled(!busy && !testing);
		reconnect->setEnabled(!busy && !testing && activeSettings.enabled);
		reconnect->setVisible(!activeSettings.onDemandApi());
		status->setText(QStringLiteral("当前状态：") + (activeSettings.onDemandApi() && !runtime.starting() &&
										!runtime.endpoint().ready() &&
										!runtime.hasFailure()
									? QStringLiteral("按需连接待机")
									: runtime.status()));
		const auto counters = runtime.endpoint().counters;
		const quint64 up = counters->uploaded.load(), down = counters->downloaded.load();
		const double seconds = double((std::max)(sample.restart(), qint64(1))) / 1000.0;
		const quint64 upRate = quint64(double(up >= lastUp ? up - lastUp : up) / seconds);
		const quint64 downRate = quint64(double(down >= lastDown ? down - lastDown : down) / seconds);
		traffic->setText(QStringLiteral("上传 %1/s    下载 %2/s\n本次代理累计：上传 %3    下载 %4")
					 .arg(bytes(upRate), bytes(downRate), bytes(up), bytes(down)));
		lastUp = up;
		lastDown = down;
	};
	QObject::connect(&refresh, &QTimer::timeout, &dialog, update);
	refresh.start(1000);
	update();
	auto probe = [&] {
		if (!testing || !probeRuntime)
			return;
		if (!probeRuntime->endpoint().ready()) {
			if (!requested && !probeRuntime->starting() &&
			    probeRuntime->status() != QStringLiteral("代理已关闭")) {
				testing = false;
				result->setText(probeRuntime->status());
				update();
			}
			return;
		}
		if (requested)
			return;
		requested = true;
		const auto endpoint = probeRuntime->endpoint();
		elapsed.start();
		const QPointer<QDialog> guard(&dialog);
		auto response = std::make_shared<Http::HttpResponse>();
		auto complete = [&, guard, response, endpoint] {
			if (!guard)
				return;
			QString ip, region;
			const auto body = QByteArray::fromStdString(response->data).left(32 * 1024);
			for (const auto &line : body.split('\n')) {
				if (line.startsWith("ip="))
					ip = QString::fromLatin1(line.mid(3)).trimmed();
				if (line.startsWith("loc="))
					region = QString::fromLatin1(line.mid(4)).trimmed();
			}
			if (endpoint.ready() && response->status == 200 && !ip.isEmpty())
				result->setText(
					QStringLiteral("连接成功 · HTTPS 延迟 %1 ms\n出口 IP：%2    地区代码：%3")
						.arg(elapsed.elapsed())
						.arg(ip, region));
			else {
				QString reason;
				if (!endpoint.ready())
					reason = QStringLiteral("检测过程中代理隧道已断开，请重新连接。");
				else if (response->networkError == CURLE_COULDNT_RESOLVE_HOST)
					reason = QStringLiteral("VPN 内 DNS 未能解析检测域名。");
				else if (response->timeout || response->cancelled)
					reason = QStringLiteral("HTTPS 请求超时，请稍后重试或切换 TCP / UDP。");
				else if (response->networkError == CURLE_SSL_CONNECT_ERROR ||
					 response->networkError == CURLE_PEER_FAILED_VERIFICATION)
					reason = QStringLiteral(
						"HTTPS TLS 握手或证书校验失败，请检查系统时间及证书配置。");
				else if (response->status != 0)
					reason = QStringLiteral("检测服务返回了无效响应（HTTP %1）。")
							 .arg(response->status);
				else
					reason = QStringLiteral("HTTPS 网络请求失败（错误码 %1，HTTP %2）。")
							 .arg(response->networkError)
							 .arg(response->status);
				result->setText(QStringLiteral("检测失败：") + reason);
			}
			testing = false;
			tester.stop();
			update();
		};
		if (!Http::HttpClient::enqueue([response, endpoint, complete = std::move(complete)] {
			    *response = Http::HttpClient::getViaProxy("https://www.cloudflare.com/cdn-cgi/trace",
								      endpoint, {}, 12000);
			    if (auto *app = QCoreApplication::instance())
				    QMetaObject::invokeMethod(app, std::move(complete), Qt::QueuedConnection);
		    })) {
			testing = false;
			tester.stop();
			result->setText(QStringLiteral("检测失败：网络请求队列不可用。"));
			update();
		}
	};
	QObject::connect(&tester, &Proxy::Runtime::changed, &dialog, [&] {
		if (probeRuntime == &tester)
			probe();
	});
	QObject::connect(&runtime, &Proxy::Runtime::changed, &dialog, [&] {
		if (probeRuntime == &runtime)
			probe();
	});
	QObject::connect(test, &QPushButton::clicked, &dialog, [&] {
		if (locked() || testing)
			return;
		persist();
		if (dirty) {
			result->setText(QStringLiteral("修改未保存，未进行检测。请查看下方保存提示。"));
			scroll->ensureWidgetVisible(result);
			return;
		}
		QString error;
		const auto settings = candidate();
		if (!Proxy::validateNode(settings, error)) {
			result->setText(error);
			scroll->ensureWidgetVisible(result);
			return;
		}
		const bool sameNode = settings.backend == activeSettings.backend &&
				      (settings.backend == "ss"
					       ? settings.uri == activeSettings.uri
					       : settings.ovpnProfile == activeSettings.ovpnProfile &&
							 settings.ovpnUsername == activeSettings.ovpnUsername &&
							 settings.ovpnPassword == activeSettings.ovpnPassword);
		if (settings.backend == "openvpn" && activeSettings.backend == "openvpn" &&
		    (runtime.starting() || (runtime.endpoint().ready() && !sameNode))) {
			result->setText(QStringLiteral("VPN 正在连接，请等待连接和新设置应用完成后再检测。"));
			return;
		}
		testing = true;
		requested = false;
		probeRuntime = sameNode && runtime.endpoint().ready() ? &runtime : &tester;
		result->setText(probeRuntime == &runtime ? QStringLiteral("正在通过当前隧道检测 HTTPS…")
							 : QStringLiteral("正在检测节点…"));
		update();
		if (probeRuntime == &runtime)
			probe();
		else
			tester.start(settings);
	});
	QTimer autosave;
	autosave.setSingleShot(true);
	autosave.setInterval(700);
	auto sameSettings = [](const Proxy::Settings &a, const Proxy::Settings &b) {
		return a.enabled == b.enabled && a.api == b.api && a.stream == b.stream && a.uri == b.uri &&
		       a.backend == b.backend && a.ovpnProfile == b.ovpnProfile && a.ovpnUsername == b.ovpnUsername &&
		       a.ovpnPassword == b.ovpnPassword && a.keepConnected == b.keepConnected;
	};
	persist = [&] {
		if (confirmingClose)
			return;
		if (locked() || testing) {
			deferredSave = dirty;
			if (dirty)
				saveStatus->setText(QStringLiteral("修改尚未保存，当前操作结束后将自动保存。"));
			return;
		}
		autosave.stop();
		const auto settings = candidate();
		if (sameSettings(settings, activeSettings)) {
			dirty = false;
			saveStatus->setText(QStringLiteral("设置已保存。"));
			return;
		}
		QString error;
		if (!Proxy::validate(settings, error) || !apply(settings, error)) {
			saveStatus->setText(QStringLiteral("未保存，仍使用原设置：") + error);
			// Failed changes must not make the toggles claim a configuration is active.
			const QSignalBlocker b1(enabled), b2(api), b3(stream), b4(keep);
			enabled->setChecked(activeSettings.enabled);
			api->setChecked(activeSettings.api);
			stream->setChecked(activeSettings.stream);
			keep->setChecked(activeSettings.keepConnected);
			dirty = !sameSettings(candidate(), activeSettings);
		} else {
			dirty = false;
			activeSettings = settings;
			saveStatus->setText(QStringLiteral("已自动保存并应用。"));
		}
		update();
	};
	QObject::connect(&autosave, &QTimer::timeout, &dialog, [&] {
		if (dialog.isActiveWindow() && !uri->hasFocus() && !username->hasFocus() && !password->hasFocus())
			persist();
	});
	scheduleSave = [&] {
		dirty = true;
		saveStatus->setText(QStringLiteral("正在等待自动保存…"));
		autosave.start();
	};
	for (auto *toggle : {enabled, api, stream, keep})
		QObject::connect(toggle, &QCheckBox::toggled, &dialog, [&](bool) {
			dirty = true;
			persist();
		});
	for (auto *field : {uri, username, password}) {
		QObject::connect(field, &QLineEdit::textChanged, &dialog, [&](const QString &) {
			dirty = true;
			invalidateProbe();
			saveStatus->setText(QStringLiteral("输入完成后自动保存。"));
		});
		QObject::connect(field, &QLineEdit::editingFinished, &dialog, [&, field] {
			if (!field->losingWindowFocus())
				persist();
		});
	}
	for (auto *selection : {backend, presets})
		QObject::connect(selection, &QComboBox::currentIndexChanged, &dialog, [&](int) {
			invalidateProbe();
			scheduleSave();
		});
	QObject::connect(reconnect, &QPushButton::clicked, &dialog, [&] {
		if (locked() || testing)
			return;
		persist();
		if (!sameSettings(candidate(), activeSettings))
			return;
		QString error;
		if (!apply(activeSettings, error))
			result->setText(error);
	});
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	dialog.canClose = [&] {
		if (dirty)
			persist();
		if (!dirty)
			return true;
		confirmingClose = true;
		QMessageBox warning(QMessageBox::Warning, QStringLiteral("修改尚未保存"),
				    saveStatus->text() + QStringLiteral("\n可返回页面继续处理，或放弃本次修改并关闭。"),
				    QMessageBox::NoButton, &dialog);
		auto *discard = warning.addButton(QStringLiteral("放弃修改并关闭"), QMessageBox::DestructiveRole);
		auto *back = warning.addButton(QStringLiteral("返回页面"), QMessageBox::RejectRole);
		warning.setDefaultButton(back);
		warning.setEscapeButton(back);
		warning.exec();
		confirmingClose = false;
		if (warning.clickedButton() != discard)
			return false;
		dirty = deferredSave = false;
		return true;
	};
	dialog.exec();
	refresh.stop();
	testing = false;
	QObject::disconnect(&tester, nullptr, &dialog, nullptr);
	QObject::disconnect(&runtime, nullptr, &dialog, nullptr);
	tester.stop();
}
} // namespace UI
