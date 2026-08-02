#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QCoreApplication>
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
#include "core/config_manager.hpp"
#include "http_client.hpp"
#include "plugin_utils.hpp"
#include "ui/dialog_factory.hpp"
#include "ui/locale.hpp"
#include "ui/menu_manager.hpp"

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

struct AccountResult {
	Bili::Config config;
	bool requestSucceeded = false;
	bool loggedIn = false;
	bool liveStatusKnown = false;
	bool isLive = false;
	std::string message;
};

AccountResult queryAccountState(Bili::Config config)
{
	AccountResult result;
	result.config = std::move(config);
	bool loggedIn = false;
	std::string message;
	if (!Bili::BiliApi::checkLoginStatus(result.config.cookies, loggedIn, message, result.config.mid)) {
		result.message = message;
		return result;
	}

	result.requestSucceeded = true;
	result.loggedIn = loggedIn;
	result.config.login_status = loggedIn;
	if (!loggedIn) {
		result.config.cookies.clear();
		result.config.csrf_token.clear();
		result.config.mid.clear();
		result.config.managed_session = false;
		result.message = "Bilibili 登录已失效，请重新扫码登录";
		return result;
	}

	std::string roomId;
	std::string csrfToken;
	if (Bili::BiliApi::getRoomIdAndCsrf(result.config.cookies, roomId, csrfToken, message)) {
		result.config.room_id = roomId;
		result.config.csrf_token = csrfToken;
	} else {
		appendMessage(result.message, message);
	}

	bool isLive = false;
	if (Bili::BiliApi::getLiveStatus(result.config, isLive, message)) {
		result.liveStatusKnown = true;
		result.isLive = isLive;
		if (!isLive)
			result.config.managed_session = false;
	} else {
		appendMessage(result.message, message);
	}
	return result;
}

struct StartAttempt {
	Bili::Config config;
	bool started = false;
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

private:
	static void frontendEventCallback(enum obs_frontend_event event, void *privateData);

	bool enqueue(std::function<void()> task);
	void handleFrontendEvent(enum obs_frontend_event event);
	void refreshAccountState(bool notifyFailure = false);
	void finishAccountRefresh(const AccountResult &result, bool notifyFailure);
	void validateNewLogin(const std::string &cookies);
	void updateLoginStatus();
	void setControlsBusy(bool busy);
	void setLiveState(bool isLive);
	void saveConfig(bool notifyFailure = false);
	void openLiveRoom();
	void handleStartAttempt(StartAttempt result);
	void handleFaceAuthRequired(const Bili::Config &config, const std::string &faceUrl, int faceAuthCode);
	void applyRoomChanges(const std::string &baseTitle, int baseAreaId, int basePartId, const std::string &title,
			      int areaId, int partId);

	Core::ConfigManager m_config;
	UI::MenuManager *m_menu = nullptr;
	LifetimeToken m_alive = std::make_shared<std::atomic_bool>(true);
	bool m_frontendCallbackRegistered = false;
	bool m_operationInFlight = false;
	bool m_stopSyncInFlight = false;
};

BilibiliStreamPlugin::BilibiliStreamPlugin(QMainWindow *parent)
	: QObject(nullptr),
	  m_menu(new UI::MenuManager(parent->menuBar(), this))
{
	m_config.load();
	connect(m_menu, &UI::MenuManager::scanQrcodeClicked, this, &BilibiliStreamPlugin::onScanQrcode);
	connect(m_menu, &UI::MenuManager::logoutClicked, this, &BilibiliStreamPlugin::onLogout);
	connect(m_menu, &UI::MenuManager::streamToggleClicked, this, &BilibiliStreamPlugin::onStreamToggle);
	connect(m_menu, &UI::MenuManager::openRoomClicked, this, &BilibiliStreamPlugin::onOpenRoom);
	connect(m_menu, &UI::MenuManager::updateRoomInfoClicked, this, &BilibiliStreamPlugin::onUpdateRoomInfo);
	updateLoginStatus();
	setLiveState(false);
	obs_frontend_add_event_callback(&BilibiliStreamPlugin::frontendEventCallback, this);
	m_frontendCallbackRegistered = true;
	refreshAccountState(false);
}

BilibiliStreamPlugin::~BilibiliStreamPlugin()
{
	m_alive->store(false, std::memory_order_release);
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
	if (Http::HttpClient::enqueue(std::move(task)))
		return true;
	obs_log(LOG_ERROR, "Background request queue is unavailable");
	return false;
}

void BilibiliStreamPlugin::frontendEventCallback(enum obs_frontend_event event, void *privateData)
{
	auto *plugin = static_cast<BilibiliStreamPlugin *>(privateData);
	if (!plugin)
		return;
	QMetaObject::invokeMethod(
		plugin, [plugin, event]() { plugin->handleFrontendEvent(event); }, Qt::QueuedConnection);
}

void BilibiliStreamPlugin::handleFrontendEvent(enum obs_frontend_event event)
{
	if (event != OBS_FRONTEND_EVENT_STREAMING_STOPPED || m_stopSyncInFlight)
		return;
	const Bili::Config config = m_config.config();
	if (!config.managed_session) {
		obs_log(LOG_DEBUG, "Ignoring OBS stop event because no plugin-managed Bilibili session is active");
		return;
	}

	m_stopSyncInFlight = true;
	const auto alive = m_alive;
	if (!enqueue([this, alive, config]() {
		    const StopSyncResult result = synchronizeManagedStop(config);
		    postToUi(alive, [this, result]() {
			    m_stopSyncInFlight = false;
			    if (result.liveStatusKnown)
				    setLiveState(result.isLive);
			    if (!result.shouldStop || result.stopped) {
				    m_config.config().managed_session = false;
				    setLiveState(false);
				    saveConfig(false);
				    obs_log(LOG_INFO,
					    "Synchronized plugin-managed Bilibili stop after OBS stopped streaming");
			    } else {
				    obs_log(LOG_ERROR,
					    "Failed to synchronize Bilibili stop after OBS stopped streaming: %s",
					    result.message.c_str());
			    }
		    });
	    })) {
		m_stopSyncInFlight = false;
	}
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

void BilibiliStreamPlugin::finishAccountRefresh(const AccountResult &result, bool notifyFailure)
{
	if (!result.requestSucceeded) {
		setControlsBusy(false);
		obs_log(LOG_WARNING, "Failed to refresh Bilibili account state: %s", result.message.c_str());
		if (notifyFailure)
			UI::DialogFactory::message(QString::fromUtf8(result.message), UI::text("Dialog.LoginFailed"));
		return;
	}

	m_config.config() = result.config;
	updateLoginStatus();
	setControlsBusy(false);
	if (result.liveStatusKnown)
		setLiveState(result.isLive);
	if (!result.loggedIn)
		setLiveState(false);
	saveConfig(notifyFailure);
	if (!result.loggedIn && notifyFailure)
		UI::DialogFactory::message(QString::fromUtf8(result.message), UI::text("Dialog.LoginFailed"));
	if (result.loggedIn && !result.message.empty())
		obs_log(LOG_WARNING, "Bilibili account refresh completed with warnings: %s", result.message.c_str());
}

void BilibiliStreamPlugin::validateNewLogin(const std::string &cookies)
{
	Bili::Config config = m_config.config();
	config.cookies = cookies;
	config.csrf_token = Core::extractCookieValue(cookies, "bili_jct");
	config.mid.clear();
	config.managed_session = false;
	setControlsBusy(true);
	const auto alive = m_alive;
	if (!enqueue([this, alive, config]() {
		    const AccountResult result = queryAccountState(config);
		    postToUi(alive, [this, result]() { finishAccountRefresh(result, true); });
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
	const auto &config = m_config.config();
	const Core::ControlState state =
		Core::controlState(busy, config.login_status, !config.room_id.empty(), !config.cookies.empty());
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
	if (config.room_id.empty()) {
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
				    [this](const std::string &loginCookies) { validateNewLogin(loginCookies); });
		    });
	    })) {
		setControlsBusy(false);
	}
}

void BilibiliStreamPlugin::onStreamToggle()
{
	if (m_operationInFlight || !m_config.config().login_status)
		return;
	setControlsBusy(true);
	const Bili::Config config = m_config.config();
	const auto alive = m_alive;
	if (!enqueue([this, alive, config]() {
		    ToggleResult result = toggleLive(config);
		    postToUi(alive, [this, result = std::move(result)]() mutable {
			    if (!result.liveStatusKnown) {
				    setControlsBusy(false);
				    UI::DialogFactory::message(QString::fromUtf8(result.message),
							       UI::text("Dialog.OperationFailed"));
				    return;
			    }
			    setLiveState(result.wasLive);
			    if (result.wasLive) {
				    setControlsBusy(false);
				    if (result.stopped) {
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
		setControlsBusy(false);
	}
}

void BilibiliStreamPlugin::handleStartAttempt(StartAttempt result)
{
	setControlsBusy(false);
	if (result.started) {
		m_config.config() = result.config;
		m_config.config().managed_session = true;
		setLiveState(true);
		saveConfig(true);
		UI::DialogFactory::streamStarted(static_cast<QWidget *>(obs_frontend_get_main_window()),
						 result.rtmpAddress, result.rtmpCode);
		return;
	}
	if (!result.faceUrl.empty() && result.faceAuthCode != 0) {
		handleFaceAuthRequired(result.config, result.faceUrl, result.faceAuthCode);
		return;
	}
	UI::DialogFactory::message(QString::fromUtf8(result.message), UI::text("Dialog.StartFailed"));
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
	};
	auto session = std::make_shared<FaceAuthSession>();
	session->config = config;
	session->faceUrl = faceUrl;
	session->faceAuthCode = faceAuthCode;

	auto *parent = static_cast<QWidget *>(obs_frontend_get_main_window());
	const bool completed = UI::DialogFactory::faceAuth(
		parent, faceUrl,
		[this, session](UI::DialogFactory::FacePollCallback callback) {
			const auto alive = m_alive;
			const Bili::Config pollConfig = session->config;
			const int pollAuthCode = session->faceAuthCode;
			auto failureCallback = callback;
			if (!enqueue([alive, pollConfig, pollAuthCode, callback = std::move(callback)]() mutable {
				    bool identified = false;
				    std::string statusMessage;
				    const bool requestSucceeded = Bili::BiliApi::isUserIdentifiedByFaceAuth(
					    pollConfig, pollAuthCode, identified, statusMessage);
				    postToUi(alive, [callback = std::move(callback), requestSucceeded, identified,
						     statusMessage = std::move(statusMessage)]() mutable {
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
			if (!enqueue([alive, retryConfig, callback = std::move(callback), session]() mutable {
				    StartAttempt result = startLive(retryConfig);
				    postToUi(alive, [callback = std::move(callback), result = std::move(result),
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
			    UI::DialogFactory::roomSettings(
				    static_cast<QWidget *>(obs_frontend_get_main_window()), config.room_id, title,
				    areaId, partId, result.partitions,
				    [this, title, areaId, partId](const std::string &newTitle, int newAreaId,
								  int newPartId) {
					    applyRoomChanges(title, areaId, partId, newTitle, newAreaId, newPartId);
				    });
		    });
	    })) {
		setControlsBusy(false);
	}
}

void BilibiliStreamPlugin::applyRoomChanges(const std::string &baseTitle, int baseAreaId, int basePartId,
					    const std::string &title, int areaId, int partId)
{
	setControlsBusy(true);
	const Bili::Config config = m_config.config();
	const auto alive = m_alive;
	if (!enqueue([this, alive, config, baseTitle, baseAreaId, basePartId, title, areaId, partId]() {
		    RoomUpdateResult result =
			    updateRoom(config, baseTitle, baseAreaId, basePartId, title, areaId, partId);
		    postToUi(alive, [this, result = std::move(result)]() mutable {
			    setControlsBusy(false);
			    if (!result.changed) {
				    UI::DialogFactory::message(UI::text("Room.NoChanges"), UI::text("Common.Message"));
				    return;
			    }
			    QStringList messages;
			    if (result.titleUpdated)
				    messages.append(UI::text("Room.TitleUpdated"));
			    if (result.areaUpdated)
				    messages.append(UI::text("Room.AreaUpdated"));
			    if (result.titleUpdated || result.areaUpdated) {
				    m_config.config() = result.config;
				    saveConfig(true);
			    }
			    if (!result.message.empty())
				    messages.append(QString::fromUtf8(result.message));
			    UI::DialogFactory::message(messages.join(QStringLiteral("\n")),
						       result.message.empty() ? UI::text("Common.Message")
									      : UI::text("Dialog.PartialFailure"));
		    });
	    })) {
		setControlsBusy(false);
	}
}

static BilibiliStreamPlugin *plugin = nullptr;

bool obs_module_load(void)
{
	auto *mainWindow = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (!mainWindow) {
		obs_log(LOG_ERROR, "OBS frontend main window is unavailable");
		return false;
	}
	if (!Bili::BiliApi::init())
		return false;
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
