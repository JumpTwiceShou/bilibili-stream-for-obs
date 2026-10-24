#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QCoreApplication>
#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QTimer>
#include <QElapsedTimer>
#include <QSignalBlocker>
#include <QDesktopServices>
#include <QMainWindow>
#include <QMetaObject>
#include <QStringList>
#include <QUrl>
#include <atomic>
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "bilibili_api.hpp"
#include "core/api_utils.hpp"
#include "core/account_state.hpp"
#include "build_identity.hpp"
#include "core/config_manager.hpp"
#include "core/obs_service_guard.hpp"
#include "core/stream_routes.hpp"
#include "ui/stream_routes_dialog.hpp"
#include "proxy/api_request_gate.hpp"
#include "ui/native_start_filter.hpp"
#include "ui/toggle_switch.hpp"
#include "http_client.hpp"
#include "plugin_utils.hpp"
#include "ui/dialog_factory.hpp"
#include "ui/locale.hpp"
#include "ui/menu_manager.hpp"
#include "ui/proxy_dialog.hpp"
#include "proxy/proxy_service.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

namespace {
using LifetimeToken = std::shared_ptr<std::atomic_bool>;

void postToUi(const LifetimeToken &alive, std::function<void()> callback)
{
	QCoreApplication *application = QCoreApplication::instance();
	if (!application)
		return;
	QMetaObject::invokeMethod(
		application,
		[alive, callback = std::move(callback)]() {
			if (alive->load(std::memory_order_acquire))
				callback();
		},
		Qt::QueuedConnection);
}

void appendMessage(std::string &target, const std::string &message)
{
	if (message.empty())
		return;
	if (!target.empty())
		target += "\n";
	target += message;
}

using Core::AccountResult;
using Core::queryAccountState;

struct StartAttempt {
	Bili::Config config;
	bool started = false;
	bool routesResolved = false;
	std::string selectedAddress;
	std::string rtmpAddress;
	std::string rtmpCode;
	std::string faceUrl;
	int faceAuthCode = 0;
	std::string message;
};

StartAttempt startLive(Bili::Config config)
{
	StartAttempt result;
	result.config = std::move(config);
	std::string mid = result.config.mid;
	result.started = Bili::BiliApi::startLive(result.config, result.rtmpAddress, result.rtmpCode, result.message,
						  result.faceUrl, result.faceAuthCode, mid);
	result.config.mid = mid;
	return result;
}

struct ToggleResult {
	bool liveStatusKnown = false;
	bool wasLive = false;
	bool stopped = false;
	StartAttempt start;
	std::string message;
};

ToggleResult toggleLive(Bili::Config config)
{
	ToggleResult result;
	if (!Bili::BiliApi::getLiveStatus(config, result.wasLive, result.message))
		return result;
	result.liveStatusKnown = true;
	if (result.wasLive) {
		result.stopped = Bili::BiliApi::stopLive(config, result.message);
		return result;
	}

	Bili::RoomInfo roomInfo;
	if (!Bili::BiliApi::getCurrentRoomInfo(config, roomInfo, result.message)) {
		result.start.config = config;
		result.start.message = result.message;
		return result;
	}
	if (roomInfo.area_id <= 0) {
		result.message = "未能读取当前直播间分区，无法开播，请先更新直播间信息";
		result.start.config = config;
		result.start.message = result.message;
		return result;
	}
	config.area_id = roomInfo.area_id;
	config.part_id = roomInfo.part_id;
	result.start = startLive(std::move(config));
	return result;
}

struct RoomLoadResult {
	Bili::RoomInfo roomInfo;
	bool hasRoomInfo = false;
	json11::Json partitions;
	std::string message;
};

RoomLoadResult loadRoomSettings(const Bili::Config &config)
{
	RoomLoadResult result;
	std::string message;
	result.hasRoomInfo = Bili::BiliApi::getCurrentRoomInfo(config, result.roomInfo, message);
	if (!result.hasRoomInfo)
		appendMessage(result.message, message);
	result.partitions = Bili::BiliApi::getPartitionList(message);
	if (!result.partitions.is_array())
		appendMessage(result.message, message);
	return result;
}

struct RoomUpdateResult {
	Bili::Config config;
	bool changed = false;
	bool titleUpdated = false;
	bool areaUpdated = false;
	std::string message;
};

RoomUpdateResult updateRoom(Bili::Config config, const std::string &baseTitle, int baseAreaId, int basePartId,
			    const std::string &title, int areaId, int partId)
{
	RoomUpdateResult result;
	result.config = std::move(config);
	std::string message;
	if (!title.empty() && title != baseTitle) {
		result.changed = true;
		if (Bili::BiliApi::updateRoomInfo(result.config, title, message)) {
			result.config.title = title;
			result.titleUpdated = true;
		} else {
			appendMessage(result.message, message);
		}
	}
	if (areaId > 0 && (areaId != baseAreaId || partId != basePartId)) {
		result.changed = true;
		if (Bili::BiliApi::updateRoomArea(result.config, areaId, message)) {
			result.config.area_id = areaId;
			result.config.part_id = partId;
			result.areaUpdated = true;
		} else {
			appendMessage(result.message, message);
		}
	}
	return result;
}

struct StopSyncResult {
	bool liveStatusKnown = false;
	bool isLive = false;
	bool shouldStop = false;
	bool stopped = false;
	std::string message;
};

StopSyncResult synchronizeManagedStop(const Bili::Config &config)
{
	StopSyncResult result;
	result.liveStatusKnown = Bili::BiliApi::getLiveStatus(config, result.isLive, result.message);
	result.shouldStop =
		Core::shouldStopManagedSession(config.managed_session, result.liveStatusKnown, result.isLive);
	if (!result.shouldStop)
		return result;
	if (config.room_id.empty() || config.csrf_token.empty() || config.cookies.empty()) {
		result.message = "托管直播配置不完整，无法同步停止 Bilibili 直播";
		return result;
	}
	result.stopped = Bili::BiliApi::stopLive(config, result.message);
	return result;
}
} // namespace

class BilibiliStreamPlugin : public QObject {
	Q_OBJECT
public:
	explicit BilibiliStreamPlugin(QMainWindow *parent);
	~BilibiliStreamPlugin() override;

private slots:
	void onScanQrcode();
	void onLogout();
	void onStreamToggle();
	void onOpenRoom();
	void onUpdateRoomInfo();
	void onProxySettings();
	void onSyncSettings();
	void onStreamRoutes();
	bool startPreparedObs(const QString &address, QString &error);

private:
	static void frontendEventCallback(enum obs_frontend_event event, void *privateData);

	bool enqueue(std::function<void()> task);
	void handleFrontendEvent(enum obs_frontend_event event);
	void beginObsStart();
	void unlockProxyForError(const std::string &message);
	void updateProxyVisibility();
	void requestManagedStop();
	void attemptManagedStop();
	void finishManagedStop(const StopSyncResult &result);
	void refreshAccountState(bool notifyFailure = false);
	void finishAccountRefresh(const AccountResult &result, bool notifyFailure, bool candidateLogin = false);
	void validateNewLogin(const std::string &cookies);
	void updateLoginStatus();
	void setControlsBusy(bool busy);
	void setLiveState(bool isLive);
	void saveConfig(bool notifyFailure = false);
	void applyProxyRouting();
	bool proxyLocked() const;
	bool prepareProxyStream(QString &error);
	void openLiveRoom();
	void handleStartAttempt(StartAttempt result);
	void handleFaceAuthRequired(const Bili::Config &config, const std::string &faceUrl, int faceAuthCode);
	void applyRoomChanges(const UI::RoomChanges &changes, UI::RoomApplyCallback callback);

	Core::ConfigManager m_config;
	UI::MenuManager *m_menu = nullptr;
	LifetimeToken m_alive = std::make_shared<std::atomic_bool>(true);
	bool m_frontendCallbackRegistered = false;
	bool m_operationInFlight = false;
	bool m_stopSyncInFlight = false;
	bool m_startObsAfterPrepare = false;
	bool m_shuttingDown = false;
	int m_stopAttempts = 0;
	int m_stopReconnects = 0;
	QElapsedTimer m_stopElapsed;
	QTimer m_stopRetry;
	Core::ObsServiceGuard m_directService;
	Proxy::Settings m_proxySettings;
	QString m_proxyConfigPath, m_proxyCorePath;
	std::unique_ptr<Proxy::Runtime> m_proxy;
	std::unique_ptr<Proxy::ApiRequestGate> m_apiGate;
	Core::StreamRoutes m_routes;
	bool m_routesFresh = false;
	Proxy::StreamRelay m_relay;
	Proxy::ServiceGuard m_proxyService;
	std::string m_proxyTarget, m_proxyKey;
	bool m_reprepareProxy = false;
};

BilibiliStreamPlugin::BilibiliStreamPlugin(QMainWindow *parent)
	: QObject(nullptr),
	  m_menu(new UI::MenuManager(parent->menuBar(), this))
{
	m_config.load();
	char *proxyPath = obs_module_config_path("proxy.json");
	char *corePath = obs_module_file("proxy/sing-box.exe");
	m_proxyConfigPath = QString::fromUtf8(proxyPath ? proxyPath : "");
	m_proxyCorePath = QString::fromUtf8(corePath ? corePath : "");
	bfree(proxyPath);
	bfree(corePath);
	QString proxyError;
	if (!Proxy::loadSettings(m_proxyConfigPath, m_proxySettings, proxyError))
		obs_log(LOG_WARNING, "Saved proxy settings could not be loaded; direct fallback is blocked");
	m_stopRetry.setSingleShot(true);
	connect(&m_stopRetry, &QTimer::timeout, this, &BilibiliStreamPlugin::attemptManagedStop);
	m_proxy = std::make_unique<Proxy::Runtime>(m_proxyCorePath);
	m_proxy->setAutoReconnect(true);
	m_apiGate = std::make_unique<Proxy::ApiRequestGate>(
		*m_proxy, [this] { return m_proxySettings; },
		[](std::function<void()> task) { return Http::HttpClient::enqueue(std::move(task)); });
	connect(m_proxy.get(), &Proxy::Runtime::changed, this, [this] {
		Http::HttpClient::setProxy(m_proxySettings.apiEnabled(), m_proxy->endpoint());
		if (!m_proxy->endpoint().ready()) {
			m_proxyService.invalidate();
			if (!m_stopSyncInFlight && m_config.config().managed_session && m_proxySettings.streamEnabled())
				m_reprepareProxy = !m_proxyTarget.empty();
		} else if (m_reprepareProxy && m_config.config().managed_session && m_proxySettings.streamEnabled()) {
			m_reprepareProxy = false;
			QString error;
			if (!prepareProxyStream(error))
				obs_log(LOG_WARNING, "Could not reprepare Bilibili proxy stream");
		}
	});
	connect(&m_relay, &Proxy::StreamRelay::failed, this, [this](const QString &) {
		m_proxyService.invalidate();
		obs_log(LOG_WARNING, "Bilibili stream proxy connection failed; no direct fallback");
	});
	applyProxyRouting();
	connect(m_menu, &UI::MenuManager::scanQrcodeClicked, this, &BilibiliStreamPlugin::onScanQrcode);
	connect(m_menu, &UI::MenuManager::logoutClicked, this, &BilibiliStreamPlugin::onLogout);
	connect(m_menu, &UI::MenuManager::streamToggleClicked, this, &BilibiliStreamPlugin::onStreamToggle);
	connect(m_menu, &UI::MenuManager::openRoomClicked, this, &BilibiliStreamPlugin::onOpenRoom);
	connect(m_menu, &UI::MenuManager::updateRoomInfoClicked, this, &BilibiliStreamPlugin::onUpdateRoomInfo);
	connect(m_menu, &UI::MenuManager::proxySettingsClicked, this, &BilibiliStreamPlugin::onProxySettings);
	connect(m_menu, &UI::MenuManager::syncSettingsClicked, this, &BilibiliStreamPlugin::onSyncSettings);
	connect(m_menu, &UI::MenuManager::streamRoutesClicked, this, &BilibiliStreamPlugin::onStreamRoutes);
	updateProxyVisibility();
	if (auto *button = parent->findChild<QPushButton *>("streamButton")) {
		new UI::NativeStartFilter(
			button, [this] { return m_config.config().sync_obs_start && !obs_frontend_streaming_active(); },
			[this] { beginObsStart(); }, this);
	} else {
		obs_log(LOG_ERROR, "Native OBS stream button unavailable; start synchronization cannot be installed");
	}
	updateLoginStatus();
	setLiveState(false);
	obs_frontend_add_event_callback(&BilibiliStreamPlugin::frontendEventCallback, this);
	m_frontendCallbackRegistered = true;
	if (m_proxySettings.apiEnabled() && m_proxy->starting()) {
		// Initial account refresh waits for the configured proxy instead of using a direct request.
		auto connection = std::make_shared<QMetaObject::Connection>();
		*connection = connect(m_proxy.get(), &Proxy::Runtime::changed, this, [this, connection] {
			if (m_proxy->starting())
				return;
			disconnect(*connection);
			refreshAccountState(false);
		});
	} else
		refreshAccountState(false);
}

BilibiliStreamPlugin::~BilibiliStreamPlugin()
{
	m_alive->store(false, std::memory_order_release);
	m_stopRetry.stop();
	m_apiGate.reset();
	m_directService.restore();
	m_proxyService.restore();
	m_relay.stop();
	if (m_proxy) {
		disconnect(m_proxy.get(), nullptr, this, nullptr);
		m_proxy->stop();
	}
	Http::HttpClient::setProxy(true, {});
	if (m_frontendCallbackRegistered) {
		obs_frontend_remove_event_callback(&BilibiliStreamPlugin::frontendEventCallback, this);
		m_frontendCallbackRegistered = false;
	}
	delete m_menu;
	m_menu = nullptr;
	obs_log(LOG_DEBUG, "Released BilibiliStreamPlugin resources");
}

bool BilibiliStreamPlugin::enqueue(std::function<void()> task)
{
	if (m_apiGate->enqueue(std::move(task)))
		return true;
	obs_log(LOG_ERROR, "Background request queue is unavailable");
	return false;
}

void BilibiliStreamPlugin::frontendEventCallback(enum obs_frontend_event event, void *privateData)
{
	auto *plugin = static_cast<BilibiliStreamPlugin *>(privateData);
	if (!plugin)
		return;
	// OBS emits profile-changing/exit on its UI thread. Restore before OBS replaces or saves the service.
	const auto delivery = (event == OBS_FRONTEND_EVENT_PROFILE_CHANGING || event == OBS_FRONTEND_EVENT_EXIT)
				      ? Qt::AutoConnection
				      : Qt::QueuedConnection;
	QMetaObject::invokeMethod(plugin, [plugin, event]() { plugin->handleFrontendEvent(event); }, delivery);
}

void BilibiliStreamPlugin::handleFrontendEvent(enum obs_frontend_event event)
{
	if (event == OBS_FRONTEND_EVENT_EXIT || event == OBS_FRONTEND_EVENT_PROFILE_CHANGING) {
		m_shuttingDown = event == OBS_FRONTEND_EVENT_EXIT;
		m_startObsAfterPrepare = false;
		m_directService.restore();
		m_reprepareProxy = false;
		m_proxyService.restore();
		m_relay.stop();
		m_proxyTarget.clear();
		m_proxyKey.clear();
	}
	if (event != OBS_FRONTEND_EVENT_STREAMING_STOPPED)
		return;
	m_directService.restore();
	m_proxyService.invalidate();
	m_relay.stop();
	m_reprepareProxy = false;
	if (m_config.config().sync_obs_stop && m_config.config().managed_session)
		requestManagedStop();
	else {
		m_proxyService.restore();
		m_proxyTarget.clear();
		m_proxyKey.clear();
	}
}

void BilibiliStreamPlugin::beginObsStart()
{
	if (m_operationInFlight || m_stopSyncInFlight || m_shuttingDown || obs_frontend_streaming_active())
		return;
	if (!m_config.config().login_status) {
		UI::DialogFactory::message(UI::text("Sync.LoginRequired"), UI::text("Dialog.StartFailed"));
		return;
	}
	if (m_config.config().room_id.empty()) {
		UI::DialogFactory::message(UI::text("Room.IdMissingLogin"), UI::text("Dialog.StartFailed"));
		return;
	}
	if (m_proxySettings.streamEnabled() && !m_proxy->endpoint().ready()) {
		UI::DialogFactory::message(UI::text("Sync.ProxyUnavailable"), UI::text("Dialog.StartFailed"));
		return;
	}
	m_startObsAfterPrepare = true;
	setControlsBusy(true);
	const auto config = m_config.config();
	const auto alive = m_alive;
	if (!enqueue([this, alive, config]() mutable {
		    StartAttempt result;
		    result.config = config;
		    Bili::RoomInfo room;
		    if (Bili::BiliApi::getCurrentRoomInfo(config, room, result.message) && room.area_id > 0) {
			    result.config.area_id = room.area_id;
			    result.config.part_id = room.part_id;
			    result = startLive(result.config);
		    } else if (result.message.empty())
			    result.message = "未能读取直播分区，请先更新直播间信息";
		    postToUi(alive,
			     [this, result = std::move(result)]() mutable { handleStartAttempt(std::move(result)); });
	    })) {
		m_startObsAfterPrepare = false;
		setControlsBusy(false);
	}
}

void BilibiliStreamPlugin::requestManagedStop()
{
	if (m_stopSyncInFlight || !m_config.config().managed_session)
		return;
	m_startObsAfterPrepare = false;
	m_stopSyncInFlight = true;
	setControlsBusy(true);
	m_stopAttempts = 0;
	m_stopReconnects = 0;
	m_stopElapsed.start();
	attemptManagedStop();
}

void BilibiliStreamPlugin::attemptManagedStop()
{
	if (!m_stopSyncInFlight || m_shuttingDown)
		return;
	const bool recoveryExhausted = m_stopReconnects >= 3 && !m_proxy->starting() && !m_proxy->endpoint().ready();
	if (m_stopElapsed.elapsed() > 180000 || m_stopAttempts >= 3 || recoveryExhausted) {
		m_stopSyncInFlight = false;
		setControlsBusy(false);
		UI::DialogFactory::message(UI::text("Sync.StopFailed"), UI::text("Dialog.StopFailed"));
		return;
	}
	if (m_proxySettings.apiEnabled() && !m_proxy->endpoint().ready()) {
		if (!m_proxy->starting()) {
			++m_stopReconnects;
			m_proxy->start(m_proxySettings);
		}
		m_stopRetry.start(1000);
		return;
	}
	++m_stopAttempts;
	const auto config = m_config.config();
	const auto alive = m_alive;
	if (!enqueue([this, alive, config] {
		    const auto result = synchronizeManagedStop(config);
		    postToUi(alive, [this, result] { finishManagedStop(result); });
	    })) {
		m_stopSyncInFlight = false;
		setControlsBusy(false);
		UI::DialogFactory::message(UI::text("Sync.StopFailed"), UI::text("Dialog.StopFailed"));
	}
}

void BilibiliStreamPlugin::finishManagedStop(const StopSyncResult &result)
{
	if (result.liveStatusKnown)
		setLiveState(result.isLive);
	if (!result.shouldStop || result.stopped) {
		m_stopSyncInFlight = false;
		setControlsBusy(false);
		m_stopRetry.stop();
		if (!obs_frontend_streaming_active()) {
			m_directService.restore();
			m_proxyService.restore();
			m_relay.stop();
		}
		m_proxyTarget.clear();
		m_proxyKey.clear();
		m_config.config().managed_session = false;
		setLiveState(false);
		saveConfig(false);
		obs_log(LOG_INFO, "Synchronized managed Bilibili stop");
		return;
	}
	// Retain ownership until an acknowledged stop or an explicit offline status.
	obs_log(LOG_WARNING, "Bilibili stop failed; retry %d/3 pending", m_stopAttempts);
	if (m_proxySettings.apiEnabled() && m_stopAttempts < 3 &&
	    (!result.liveStatusKnown || !m_proxy->endpoint().ready()))
		m_proxy->start(m_proxySettings);
	m_stopRetry.start(2000);
}

void BilibiliStreamPlugin::refreshAccountState(bool notifyFailure)
{
	const Bili::Config config = m_config.config();
	if (config.cookies.empty()) {
		updateLoginStatus();
		setControlsBusy(false);
		return;
	}
	setControlsBusy(true);
	const auto alive = m_alive;
	if (!enqueue([this, alive, config, notifyFailure]() {
		    const AccountResult result = queryAccountState(config);
		    postToUi(alive, [this, result, notifyFailure]() { finishAccountRefresh(result, notifyFailure); });
	    })) {
		setControlsBusy(false);
	}
}

void BilibiliStreamPlugin::finishAccountRefresh(const AccountResult &result, bool notifyFailure, bool candidateLogin)
{
	if (!result.requestSucceeded) {
		setControlsBusy(false);
		obs_log(LOG_WARNING, "Failed to refresh Bilibili account state: %s", result.message.c_str());
		if (notifyFailure)
			UI::DialogFactory::message(QString::fromUtf8(result.message), UI::text("Dialog.LoginFailed"));
		return;
	}

	const bool accountChanged = Core::accountIdentity(m_config.config()) != Core::accountIdentity(result.config);
	const bool stoppedSinceSnapshot = !m_config.config().managed_session && result.config.managed_session;
	const auto decision = Core::applyAccountRefresh(m_config.config(), result, candidateLogin);
	if (decision != Core::AccountApplyDecision::Applied) {
		setControlsBusy(false);
		UI::DialogFactory::message(decision == Core::AccountApplyDecision::ManagedAccountChange
						   ? UI::text("Login.ManagedAccountChange")
						   : QString::fromUtf8(result.message),
					   UI::text("Dialog.LoginFailed"));
		return;
	}
	const auto &next = m_config.config();
	if (accountChanged || !result.loggedIn || next.last_rtmp_key.empty()) {
		m_routes.clear();
		m_routesFresh = false;
		m_proxyTarget.clear();
		m_proxyKey.clear();
		m_reprepareProxy = false;
		if (!obs_frontend_streaming_active() && !next.managed_session) {
			m_directService.restore();
			m_proxyService.restore();
			m_relay.stop();
		}
	}
	updateLoginStatus();
	setControlsBusy(false);
	if (result.liveStatusKnown && !stoppedSinceSnapshot)
		setLiveState(result.isLive);
	if (!result.loggedIn)
		setLiveState(false);
	saveConfig(notifyFailure);
	if (!result.loggedIn && notifyFailure)
		UI::DialogFactory::message(QString::fromUtf8(result.message), UI::text("Dialog.LoginFailed"));
	if (result.loggedIn && !result.message.empty()) {
		obs_log(LOG_WARNING, "Bilibili account refresh completed with warnings: %s", result.message.c_str());
		if (notifyFailure)
			UI::DialogFactory::message(QString::fromUtf8(result.message), UI::text("Login.RoomWarning"));
	}
}

void BilibiliStreamPlugin::validateNewLogin(const std::string &cookies)
{
	const Bili::Config config = Core::prepareAccountLogin(m_config.config(), cookies);
	if (m_config.config().managed_session &&
	    Core::accountIdentity(m_config.config()) != Core::accountIdentity(config)) {
		setControlsBusy(false);
		UI::DialogFactory::message(UI::text("Login.ManagedAccountChange"), UI::text("Dialog.LoginFailed"));
		return;
	}
	setControlsBusy(true);
	const auto alive = m_alive;
	if (!enqueue([this, alive, config]() {
		    const AccountResult result = queryAccountState(config);
		    postToUi(alive, [this, result]() { finishAccountRefresh(result, true, true); });
	    })) {
		setControlsBusy(false);
	}
}

void BilibiliStreamPlugin::updateLoginStatus()
{
	const auto &config = m_config.config();
	m_menu->actions().loginStatus->setText(config.login_status ? UI::text("Menu.LoginStatus.LoggedIn")
								   : UI::text("Menu.LoginStatus.LoggedOut"));
	m_menu->actions().loginStatus->setChecked(config.login_status);
}

void BilibiliStreamPlugin::setControlsBusy(bool busy)
{
	m_operationInFlight = busy;
	if (m_apiGate)
		m_apiGate->setHeld(busy || m_stopSyncInFlight);
	const auto &config = m_config.config();
	const Core::ControlState state = Core::controlState(busy || m_stopSyncInFlight, config.login_status,
							    !config.room_id.empty(), !config.cookies.empty());
	m_menu->actions().scanQrcode->setEnabled(state.scanQrEnabled);
	m_menu->actions().logout->setEnabled(state.logoutEnabled);
	m_menu->actions().streamToggle->setEnabled(state.streamToggleEnabled);
	m_menu->actions().updateRoomInfo->setEnabled(state.updateRoomEnabled);
	m_menu->actions().openRoom->setEnabled(state.openRoomEnabled);
}

void BilibiliStreamPlugin::onLogout()
{
	if (m_operationInFlight)
		return;
	const Bili::Config previousConfig = m_config.config();
	if (previousConfig.cookies.empty())
		return;
	if (previousConfig.managed_session) {
		UI::DialogFactory::message(UI::text("Logout.ManagedBlocked"), UI::text("Dialog.LogoutFailed"));
		return;
	}
	if (!UI::DialogFactory::confirm(UI::text("Logout.Confirm"), UI::text("Dialog.Logout")))
		return;

	m_config.config() = Bili::Config();
	m_config.config().sync_obs_start = previousConfig.sync_obs_start;
	m_config.config().sync_obs_stop = previousConfig.sync_obs_stop;
	m_config.config().proxy_unlocked = previousConfig.proxy_unlocked;
	if (!m_config.save()) {
		m_config.config() = previousConfig;
		updateLoginStatus();
		setControlsBusy(false);
		UI::DialogFactory::message(UI::text("Logout.SaveFailed"), UI::text("Dialog.LogoutFailed"));
		return;
	}
	updateLoginStatus();
	setLiveState(false);
	setControlsBusy(false);
	m_routes.clear();
	m_routesFresh = false;
	UI::DialogFactory::message(UI::text("Logout.Success"), UI::text("Dialog.Logout"));
}

void BilibiliStreamPlugin::setLiveState(bool isLive)
{
	m_menu->actions().streamToggle->setText(isLive ? UI::text("Menu.StopLive") : UI::text("Menu.StartLive"));
}

void BilibiliStreamPlugin::saveConfig(bool notifyFailure)
{
	if (!m_config.save() && notifyFailure) {
		UI::DialogFactory::message(UI::text("Config.SaveFailed"), UI::text("Dialog.ConfigError"));
	}
}

void BilibiliStreamPlugin::openLiveRoom()
{
	const auto &config = m_config.config();
	if (!config.login_status || config.room_id.empty()) {
		UI::DialogFactory::message(UI::text("Room.IdMissing"), UI::text("Common.Message"));
		return;
	}
	const QString url = QStringLiteral("https://live.bilibili.com/%1").arg(QString::fromStdString(config.room_id));
	if (!QDesktopServices::openUrl(QUrl(url))) {
		UI::DialogFactory::message(UI::text("Room.OpenFailed") + url, UI::text("Common.Message"));
	}
}

void BilibiliStreamPlugin::onOpenRoom()
{
	openLiveRoom();
}

void BilibiliStreamPlugin::onScanQrcode()
{
	if (m_operationInFlight)
		return;
	setControlsBusy(true);
	const std::string cookies = m_config.config().cookies;
	const auto alive = m_alive;
	if (!enqueue([this, alive, cookies]() {
		    std::string qrData;
		    std::string qrKey;
		    std::string message;
		    const bool succeeded = Bili::BiliApi::getQrCode(cookies, qrData, qrKey, message);
		    postToUi(alive, [this, succeeded, qrData, qrKey, message]() {
			    setControlsBusy(false);
			    if (!succeeded) {
				    UI::DialogFactory::message(QString::fromUtf8(message),
							       UI::text("Dialog.LoginFailed"));
				    return;
			    }
			    auto *parent = static_cast<QWidget *>(obs_frontend_get_main_window());
			    setControlsBusy(true);
			    bool validatingLogin = false;
			    UI::DialogFactory::qrLogin(
				    parent, qrData,
				    [this, qrKey](UI::DialogFactory::QrPollCallback callback) {
					    const auto pollAlive = m_alive;
					    auto failureCallback = callback;
					    if (!enqueue([pollAlive, qrKey, callback = std::move(callback)]() mutable {
							std::string loginCookies;
							std::string statusMessage;
							const auto result = Bili::BiliApi::qrLogin(qrKey, loginCookies,
												   statusMessage);
							postToUi(pollAlive,
								 [callback = std::move(callback), result,
								  loginCookies = std::move(loginCookies),
								  statusMessage = std::move(statusMessage)]() mutable {
									 callback(result, std::move(loginCookies),
										  std::move(statusMessage));
								 });
						})) {
						    failureCallback(Bili::BiliApi::QrLoginResult::Error, {},
								    "后台请求队列不可用");
					    }
				    },
				    [this, &validatingLogin](const std::string &loginCookies) {
					    validatingLogin = true;
					    validateNewLogin(loginCookies);
				    });
			    if (!validatingLogin)
				    setControlsBusy(false);
		    });
	    })) {
		setControlsBusy(false);
	}
}

void BilibiliStreamPlugin::onStreamToggle()
{
	if (m_operationInFlight || m_stopSyncInFlight || !m_config.config().login_status)
		return;
	if (m_config.config().managed_session) {
		requestManagedStop();
		return;
	}
	if (m_proxySettings.streamEnabled() && !m_config.config().managed_session && !m_proxy->endpoint().ready()) {
		UI::DialogFactory::message(QStringLiteral("代理未就绪，请打开代理设置检测节点或重新保存。"),
					   UI::text("Dialog.StartFailed"));
		return;
	}
	if (obs_frontend_streaming_active() && !m_config.config().managed_session) {
		UI::DialogFactory::message(QStringLiteral("请先停止当前 OBS 推流，再通过插件准备直播。"),
					   UI::text("Dialog.StartFailed"));
		return;
	}
	m_startObsAfterPrepare = false;
	setControlsBusy(true);
	const Bili::Config config = m_config.config();
	const auto alive = m_alive;
	if (!enqueue([this, alive, config]() {
		    ToggleResult result = toggleLive(config);
		    postToUi(alive, [this, result = std::move(result)]() mutable {
			    if (!result.liveStatusKnown) {
				    m_startObsAfterPrepare = false;
				    setControlsBusy(false);
				    UI::DialogFactory::message(QString::fromUtf8(result.message),
							       UI::text("Dialog.OperationFailed"));
				    return;
			    }
			    setLiveState(result.wasLive);
			    if (result.wasLive) {
				    m_startObsAfterPrepare = false;
				    setControlsBusy(false);
				    if (result.stopped) {
					    if (!obs_frontend_streaming_active()) {
						    m_proxyService.restore();
						    m_relay.stop();
					    }
					    m_config.config().managed_session = false;
					    setLiveState(false);
					    saveConfig(true);
					    UI::DialogFactory::message(UI::text("Room.LiveStopped"),
								       UI::text("Common.Message"));
				    } else {
					    UI::DialogFactory::message(QString::fromUtf8(result.message),
								       UI::text("Dialog.StopFailed"));
				    }
				    return;
			    }
			    handleStartAttempt(std::move(result.start));
		    });
	    })) {
		m_startObsAfterPrepare = false;
		setControlsBusy(false);
	}
}

void BilibiliStreamPlugin::handleStartAttempt(StartAttempt result)
{
	if (result.started && !result.routesResolved) {
		result.config.proxy_unlocked |= m_config.config().proxy_unlocked;
		result.config.managed_session = true;
		result.config.last_rtmp_address = result.rtmpAddress;
		result.config.last_rtmp_key = result.rtmpCode;
		result.config.selected_rtmp_address = result.rtmpAddress;
		m_config.config() = result.config;
		saveConfig(false);
		m_routes = Core::obsStreamRoutes();
		bool found = false;
		for (auto &route : m_routes) {
			if (route.url == QString::fromStdString(result.rtmpAddress)) {
				found = true;
				route.name += QStringLiteral(" · 本次 B 站返回");
			}
		}
		if (!found)
			m_routes.prepend({QStringLiteral("本次 B 站返回的地址"),
					  QString::fromStdString(result.rtmpAddress), -1});
		result.routesResolved = true;
		if (m_proxySettings.streamEnabled()) {
			result.selectedAddress = result.rtmpAddress;
			handleStartAttempt(std::move(result));
		} else {
			Core::measureDirectRoutes(
				this, m_routes, [this, result = std::move(result)](Core::StreamRoutes routes) mutable {
					m_routes = std::move(routes);
					m_routesFresh = true;
					const int selected = Core::preferredStreamRoute(
						m_routes, QString::fromStdString(result.rtmpAddress), false);
					result.selectedAddress = selected >= 0 ? m_routes[selected].url.toStdString()
									       : result.rtmpAddress;
					handleStartAttempt(std::move(result));
				});
		}
		return;
	}
	if (result.started) {
		result.config.proxy_unlocked |= m_config.config().proxy_unlocked;
		result.config.last_rtmp_address = result.rtmpAddress;
		result.config.last_rtmp_key = result.rtmpCode;
		result.config.selected_rtmp_address = result.selectedAddress;
		m_config.config() = result.config;
		m_config.config().managed_session = true;
		setLiveState(true);
		saveConfig(true);
		if (m_proxySettings.streamEnabled()) {
			m_proxyTarget = result.selectedAddress;
			m_proxyKey = result.rtmpCode;
			QString error;
			if (!prepareProxyStream(error)) {
				m_startObsAfterPrepare = false;
				setControlsBusy(false);
				UI::DialogFactory::message(
					QStringLiteral("B 站已开播，但代理推流准备失败：\n") + error +
						QStringLiteral("\n请先通过插件停止 B 站直播，再检查代理设置。"),
					UI::text("Dialog.StartFailed"));
				return;
			}
			setControlsBusy(false);
			if (std::exchange(m_startObsAfterPrepare, false)) {
				obs_frontend_streaming_start();
				return;
			}
			onStreamRoutes();
			return;
		}
		setControlsBusy(false);
		if (std::exchange(m_startObsAfterPrepare, false)) {
			if (!m_directService.install(result.selectedAddress, result.rtmpCode)) {
				UI::DialogFactory::message(UI::text("Sync.PrepareFailed"),
							   UI::text("Dialog.StartFailed"));
				return;
			}
			obs_frontend_streaming_start();
			return;
		}
		onStreamRoutes();
		return;
	}
	if (!result.faceUrl.empty() && result.faceAuthCode != 0) {
		handleFaceAuthRequired(result.config, result.faceUrl, result.faceAuthCode);
		return;
	}
	m_startObsAfterPrepare = false;
	setControlsBusy(false);
	unlockProxyForError(result.message);
	const QString help = Core::isIpFollowerRestriction(result.message)
				     ? QStringLiteral("\n可从 Bilibili 菜单的“代理设置”配置接口连接。")
				     : QString();
	UI::DialogFactory::message(QString::fromUtf8(result.message) + help, UI::text("Dialog.StartFailed"));
}

void BilibiliStreamPlugin::handleFaceAuthRequired(const Bili::Config &config, const std::string &faceUrl,
						  int faceAuthCode)
{
	struct FaceAuthSession {
		Bili::Config config;
		std::string faceUrl;
		int faceAuthCode = 0;
		StartAttempt completedAttempt;
		bool hasCompletedAttempt = false;
		int lastPollState = -1;
	};
	auto session = std::make_shared<FaceAuthSession>();
	session->config = config;
	session->faceUrl = faceUrl;
	session->faceAuthCode = faceAuthCode;

	auto *parent = static_cast<QWidget *>(obs_frontend_get_main_window());
	setControlsBusy(true);
	const bool completed = UI::DialogFactory::faceAuth(
		parent, faceUrl,
		[this, session](UI::DialogFactory::FacePollCallback callback) {
			const auto alive = m_alive;
			const Bili::Config pollConfig = session->config;
			const int pollAuthCode = session->faceAuthCode;
			auto failureCallback = callback;
			if (!enqueue([alive, session, pollConfig, pollAuthCode,
				      callback = std::move(callback)]() mutable {
				    bool identified = false;
				    std::string statusMessage;
				    const bool requestSucceeded = Bili::BiliApi::isUserIdentifiedByFaceAuth(
					    pollConfig, pollAuthCode, identified, statusMessage);
				    postToUi(alive, [session, pollAuthCode, callback = std::move(callback),
						     requestSucceeded, identified,
						     statusMessage = std::move(statusMessage)]() mutable {
					    const int pollState = requestSucceeded ? (identified ? 2 : 1) : 0;
					    if (session->lastPollState != pollState) {
						    session->lastPollState = pollState;
						    obs_log(LOG_INFO, "Room face verification: type=%d, state=%s",
							    pollAuthCode,
							    requestSucceeded ? (identified ? "verified" : "pending")
									     : "unavailable");
					    }
					    callback(requestSucceeded, identified, std::move(statusMessage));
				    });
			    })) {
				failureCallback(false, false, "后台请求队列不可用");
			}
		},
		[this, session](UI::DialogFactory::FaceRetryCallback callback) {
			const auto alive = m_alive;
			const Bili::Config retryConfig = session->config;
			auto failureCallback = callback;
			if (!enqueue([this, alive, retryConfig, callback = std::move(callback), session]() mutable {
				    StartAttempt result = startLive(retryConfig);
				    postToUi(alive, [this, callback = std::move(callback), result = std::move(result),
						     session]() mutable {
					    const bool faceAuthRequired = !result.faceUrl.empty() &&
									  result.faceAuthCode != 0;
					    const auto decision =
						    Core::classifyFaceAuthRetry(result.started, faceAuthRequired);
					    if (decision == Core::FaceAuthRetryDecision::ContinueWaiting) {
						    session->config = result.config;
						    session->faceUrl = result.faceUrl;
						    session->faceAuthCode = result.faceAuthCode;
						    callback(decision, result.faceUrl, {});
						    return;
					    }
					    unlockProxyForError(result.message);
					    session->completedAttempt = std::move(result);
					    session->hasCompletedAttempt = true;
					    callback(decision, {}, session->completedAttempt.message);
				    });
			    })) {
				session->completedAttempt.config = retryConfig;
				session->completedAttempt.message = "后台请求队列不可用";
				session->hasCompletedAttempt = true;
				failureCallback(Core::FaceAuthRetryDecision::Failed, {},
						session->completedAttempt.message);
			}
		});

	if (completed && session->hasCompletedAttempt)
		handleStartAttempt(std::move(session->completedAttempt));
	else {
		m_startObsAfterPrepare = false;
		setControlsBusy(false);
	}
}

void BilibiliStreamPlugin::onUpdateRoomInfo()
{
	if (m_operationInFlight || !m_config.config().login_status)
		return;
	const Bili::Config config = m_config.config();
	if (config.room_id.empty()) {
		UI::DialogFactory::message(UI::text("Room.IdMissingLogin"), UI::text("Common.Message"));
		return;
	}
	setControlsBusy(true);
	const auto alive = m_alive;
	if (!enqueue([this, alive, config]() {
		    RoomLoadResult result = loadRoomSettings(config);
		    postToUi(alive, [this, config, result = std::move(result)]() mutable {
			    setControlsBusy(false);
			    if (!result.message.empty())
				    obs_log(LOG_WARNING, "Room settings loaded with warnings: %s",
					    result.message.c_str());
			    const std::string title = result.hasRoomInfo ? result.roomInfo.title : config.title;
			    const int areaId = result.hasRoomInfo ? result.roomInfo.area_id : config.area_id;
			    const int partId = result.hasRoomInfo ? result.roomInfo.part_id : config.part_id;
			    UI::DialogFactory::roomSettings(static_cast<QWidget *>(obs_frontend_get_main_window()),
							    config.room_id, title, areaId, partId, result.partitions,
							    [this](const UI::RoomChanges &changes,
								   UI::RoomApplyCallback callback) {
								    applyRoomChanges(changes, std::move(callback));
							    });
		    });
	    })) {
		setControlsBusy(false);
	}
}

void BilibiliStreamPlugin::applyRoomChanges(const UI::RoomChanges &changes, UI::RoomApplyCallback callback)
{
	setControlsBusy(true);
	const Bili::Config config = m_config.config();
	const auto alive = m_alive;
	if (!enqueue([this, alive, config, changes, callback]() {
		    RoomUpdateResult result = updateRoom(config, changes.baseTitle, changes.baseAreaId,
							 changes.basePartId, changes.title, changes.areaId,
							 changes.partId);
		    postToUi(alive, [this, callback, result = std::move(result)]() mutable {
			    setControlsBusy(false);
			    if (!result.changed) {
				    callback({false, false, UI::text("Room.NoChanges").toStdString()});
				    return;
			    }
			    if (result.titleUpdated || result.areaUpdated) {
				    m_config.config() = result.config;
				    if (!m_config.save())
					    appendMessage(result.message, UI::text("Config.SaveFailed").toStdString());
			    }
			    callback({result.titleUpdated, result.areaUpdated, std::move(result.message)});
		    });
	    })) {
		setControlsBusy(false);
		callback({false, false, UI::text("Room.ApplyFailed").toStdString()});
	}
}

static BilibiliStreamPlugin *plugin = nullptr;

bool BilibiliStreamPlugin::proxyLocked() const
{
	return m_operationInFlight || m_stopSyncInFlight || obs_frontend_streaming_active();
}
bool BilibiliStreamPlugin::prepareProxyStream(QString &error)
{
	const bool prepared = m_relay.start(QString::fromStdString(m_proxyTarget), m_proxy->endpoint(), error);
	const QString relayError = error;
	const bool installed = m_proxyService.install(prepared ? m_relay.localUrl() : QString(), m_proxyKey,
						      m_proxy->endpoint(), error);
	if (!prepared)
		error = relayError;
	return prepared && installed;
}
void BilibiliStreamPlugin::applyProxyRouting()
{
	if (m_config.config().managed_session) {
		m_proxyService.invalidate();
		m_relay.stop();
		m_reprepareProxy = m_proxySettings.streamEnabled() && !m_proxyTarget.empty();
	} else {
		m_proxyService.restore();
		m_relay.stop();
		m_reprepareProxy = false;
	}
	Http::HttpClient::setProxy(m_proxySettings.apiEnabled(), {});
	if (m_proxySettings.keepRunning()) {
		m_proxy->setAutoReconnect(true);
		m_proxy->start(m_proxySettings);
	} else
		m_proxy->stop();
	Http::HttpClient::setProxy(m_proxySettings.apiEnabled(), m_proxy->endpoint());
}
void BilibiliStreamPlugin::onProxySettings()
{
	UI::proxyDialog(
		static_cast<QWidget *>(obs_frontend_get_main_window()), m_proxySettings, *m_proxy, m_proxyCorePath,
		[this] { return proxyLocked(); },
		[this](const Proxy::Settings &settings, QString &error) {
			if (!Proxy::validateChange(m_proxySettings, settings, obs_frontend_streaming_active(),
						   m_operationInFlight || m_stopSyncInFlight,
						   m_config.config().managed_session, error))
				return false;
			if (!Proxy::saveSettings(m_proxyConfigPath, settings, error))
				return false;
			m_proxySettings = settings;
			applyProxyRouting();
			return true;
		});
}

void BilibiliStreamPlugin::updateProxyVisibility()
{
	if (!m_config.config().proxy_unlocked &&
	    (m_proxySettings.enabled || !m_proxySettings.uri.isEmpty() || !m_proxySettings.ovpnProfile.isEmpty())) {
		m_config.config().proxy_unlocked = true;
		saveConfig(false);
	}
	m_menu->actions().proxySettings->setVisible(m_config.config().proxy_unlocked);
}

void BilibiliStreamPlugin::unlockProxyForError(const std::string &message)
{
	if (!Core::isIpFollowerRestriction(message) || m_config.config().proxy_unlocked)
		return;
	m_config.config().proxy_unlocked = true;
	saveConfig(false);
	updateProxyVisibility();
}

bool BilibiliStreamPlugin::startPreparedObs(const QString &address, QString &error)
{
	if (m_operationInFlight || m_stopSyncInFlight || m_shuttingDown || obs_frontend_streaming_active()) {
		error = QStringLiteral("请等待当前操作完成，或先停止正在进行的 OBS 推流。");
		return false;
	}
	const auto &config = m_config.config();
	if (!config.login_status || !config.managed_session || config.last_rtmp_key.empty()) {
		error = QStringLiteral("本次 B 站直播已结束或推流信息不可用，请重新通过插件开播。");
		return false;
	}
	const QUrl url(address, QUrl::StrictMode);
	if (!url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty() ||
	    (url.scheme() != "rtmp" && url.scheme() != "rtmps")) {
		error = QStringLiteral("请选择有效的推流线路。");
		return false;
	}
	if (m_proxySettings.streamEnabled()) {
		if (!m_proxy->endpoint().ready()) {
			error = UI::text("Sync.ProxyUnavailable");
			return false;
		}
		m_directService.restore();
		m_proxyTarget = address.toStdString();
		m_proxyKey = config.last_rtmp_key;
		if (!prepareProxyStream(error))
			return false;
	} else {
		m_proxyService.restore();
		if (!m_directService.install(address.toStdString(), config.last_rtmp_key)) {
			error = UI::text("Sync.PrepareFailed");
			return false;
		}
	}
	m_config.config().selected_rtmp_address = address.toStdString();
	saveConfig(false);
	// Use the existing Bilibili session, without another startLive request.
	obs_frontend_streaming_start();
	return true;
}

void BilibiliStreamPlugin::onStreamRoutes()
{
	if (m_operationInFlight || m_stopSyncInFlight)
		return;
	const auto &config = m_config.config();
	if (!config.login_status || config.cookies.empty()) {
		UI::DialogFactory::message(UI::text("Sync.LoginRequired"), UI::text("Common.Message"));
		return;
	}
	if (m_routes.isEmpty()) {
		m_routes = Core::obsStreamRoutes();
		if (!config.last_rtmp_address.empty())
			m_routes.prepend({QStringLiteral("上次 B 站返回的地址"),
					  QString::fromStdString(config.last_rtmp_address), -1});
	}
	if (!m_proxySettings.streamEnabled() && !m_routesFresh) {
		setControlsBusy(true);
		Core::measureDirectRoutes(this, m_routes, [this](Core::StreamRoutes routes) {
			m_routes = std::move(routes);
			m_routesFresh = true;
			setControlsBusy(false);
			onStreamRoutes();
		});
		return;
	}
	m_routesFresh = false;
	const int selected = Core::preferredStreamRoute(m_routes, QString::fromStdString(config.last_rtmp_address),
							m_proxySettings.streamEnabled());
	UI::StreamRouteActions actions;
	actions.obsActive = [] {
		return obs_frontend_streaming_active();
	};
	actions.sessionActive = [this] {
		return m_config.config().managed_session;
	};
	actions.canStartObs = [this] {
		return !m_operationInFlight && !m_stopSyncInFlight && !m_shuttingDown &&
		       m_config.config().managed_session && !obs_frontend_streaming_active();
	};
	actions.startObs = [this](const QString &address, QString &error) {
		return startPreparedObs(address, error);
	};
	UI::streamRoutesDialog(
		static_cast<QWidget *>(obs_frontend_get_main_window()), m_routes, selected, config.last_rtmp_key,
		m_proxySettings.streamEnabled(), config.managed_session,
		[this](const QString &address, QString &error) {
			if (m_operationInFlight || m_stopSyncInFlight || obs_frontend_streaming_active()) {
				error = QStringLiteral("请等待当前操作完成并停止 OBS 推流，再替换配置。");
				return false;
			}
			const auto key = m_config.config().last_rtmp_key;
			if (m_proxySettings.streamEnabled()) {
				m_directService.restore();
				m_proxyTarget = address.toStdString();
				m_proxyKey = key;
				if (!prepareProxyStream(error))
					return false;
			} else {
				m_directService.restore();
				m_proxyService.restore();
				if (!Core::applyObsStreamRoute(address, key, error))
					return false;
			}
			m_config.config().selected_rtmp_address = address.toStdString();
			if (!m_config.save()) {
				error = QStringLiteral("OBS 配置已替换，但线路记忆保存失败。");
				return false;
			}
			return true;
		},
		std::move(actions));
}

void BilibiliStreamPlugin::onSyncSettings()
{
	QDialog dialog(static_cast<QWidget *>(obs_frontend_get_main_window()));
	dialog.setWindowTitle(UI::text("Sync.Title"));
	auto *layout = new QVBoxLayout(&dialog);
	auto *start = new UI::ToggleSwitch(UI::text("Sync.Start"));
	auto *stop = new UI::ToggleSwitch(UI::text("Sync.Stop"));
	start->setChecked(m_config.config().sync_obs_start);
	stop->setChecked(m_config.config().sync_obs_stop);
	layout->addWidget(start);
	layout->addWidget(stop);
	auto *note = new QLabel(UI::text("Sync.Note"));
	note->setWordWrap(true);
	layout->addWidget(note);
	auto *status = new QLabel;
	layout->addWidget(status);
	auto persist = [&, this] {
		if (m_operationInFlight || m_stopSyncInFlight) {
			const QSignalBlocker blockStart(start), blockStop(stop);
			start->setChecked(m_config.config().sync_obs_start);
			stop->setChecked(m_config.config().sync_obs_stop);
			return;
		}
		const bool previousStart = m_config.config().sync_obs_start;
		const bool previousStop = m_config.config().sync_obs_stop;
		m_config.config().sync_obs_start = start->isChecked();
		m_config.config().sync_obs_stop = stop->isChecked();
		if (!m_config.save()) {
			m_config.config().sync_obs_start = previousStart;
			m_config.config().sync_obs_stop = previousStop;
			const QSignalBlocker blockStart(start), blockStop(stop);
			start->setChecked(previousStart);
			stop->setChecked(previousStop);
			status->setText(UI::text("Sync.SaveFailed"));
		} else
			status->setText(UI::text("Sync.Saved"));
	};
	connect(start, &QCheckBox::toggled, &dialog, persist);
	connect(stop, &QCheckBox::toggled, &dialog, persist);
	QTimer refresh;
	connect(&refresh, &QTimer::timeout, &dialog, [&, this] {
		start->setEnabled(!(m_operationInFlight || m_stopSyncInFlight));
		stop->setEnabled(!(m_operationInFlight || m_stopSyncInFlight));
	});
	start->setEnabled(!(m_operationInFlight || m_stopSyncInFlight));
	stop->setEnabled(!(m_operationInFlight || m_stopSyncInFlight));
	refresh.start(250);
	dialog.exec();
}

bool obs_module_load(void)
{
	auto *mainWindow = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (!mainWindow) {
		obs_log(LOG_ERROR, "OBS frontend main window is unavailable");
		return false;
	}
	if (!Bili::BiliApi::init())
		return false;
	Proxy::registerService();
	Core::registerManagedService();
	try {
		plugin = new BilibiliStreamPlugin(mainWindow);
	} catch (const std::exception &error) {
		obs_log(LOG_ERROR, "Plugin initialization failed: %s", error.what());
		Bili::BiliApi::cleanup();
		return false;
	} catch (...) {
		obs_log(LOG_ERROR, "Plugin initialization failed");
		Bili::BiliApi::cleanup();
		return false;
	}
	obs_log(LOG_INFO, "Plugin version: %s; build: %s; private presets: %s", PLUGIN_VERSION, BILIBILI_BUILD_COMMIT,
		BILIBILI_BUILD_PRIVATE_PRESETS);
	obs_log(LOG_INFO, "Plugin loaded successfully");
	return true;
}

void obs_module_unload(void)
{
	delete plugin;
	plugin = nullptr;
	Bili::BiliApi::cleanup();
	obs_log(LOG_INFO, "Plugin unloaded");
}

#include "plugin-main.moc"
