#include <obs-data.h>
#include <obs-frontend-api.h>
#include <obs-module.h>
#include <QDesktopServices>
#include <QMainWindow>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <vector>
#include "bilibili_api.hpp"
#include "core/config_manager.hpp"
#include "core/qr_generator.hpp"
#include "plugin_utils.hpp"
#include "ui/dialog_factory.hpp"
#include "ui/menu_manager.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

class BilibiliStreamPlugin : public QObject {
	Q_OBJECT
public:
	explicit BilibiliStreamPlugin(QMainWindow *parent);
	~BilibiliStreamPlugin();

private slots:
	void onScanQrcode();
	void onStreamToggle();
	void onOpenRoom();
	void onUpdateRoomInfo();

private:
	static void frontendEventCallback(enum obs_frontend_event event, void *private_data);

	void handleFrontendEvent(enum obs_frontend_event event);
	void updateLoginStatus();
	void openLiveRoom();
	void setStreamingState(bool streaming);
	bool queryLiveStatus(bool &is_live, std::string &message);
	bool queryCurrentRoomInfo(Bili::RoomInfo &roomInfo, std::string &message);
	bool prepareStartConfig(Bili::Config &startConfig, std::string &message);
	bool tryStartLive(const Bili::Config &startConfig, std::string &message, std::string &faceQr);
	void handleStartLiveSuccess(const std::string &rtmpAddr, const std::string &rtmpCode);
	void handleFaceAuthRequired(const Bili::Config &startConfig, const std::string &faceUrl);

	Core::ConfigManager m_config;
	UI::MenuManager *m_menu;
	bool m_frontendCallbackRegistered = false;
};

BilibiliStreamPlugin::BilibiliStreamPlugin(QMainWindow *parent)
	: QObject(nullptr), m_menu(new UI::MenuManager(parent->menuBar(), this))
{
	m_config.load();

	connect(m_menu, &UI::MenuManager::scanQrcodeClicked, this, &BilibiliStreamPlugin::onScanQrcode);
	connect(m_menu, &UI::MenuManager::streamToggleClicked, this, &BilibiliStreamPlugin::onStreamToggle);
	connect(m_menu, &UI::MenuManager::openRoomClicked, this, &BilibiliStreamPlugin::onOpenRoom);
	connect(m_menu, &UI::MenuManager::updateRoomInfoClicked, this, &BilibiliStreamPlugin::onUpdateRoomInfo);

	auto &cfg = m_config.config();
	if (!cfg.cookies.empty()) {
		std::string message;
		if (Bili::BiliApi::checkLoginStatus(cfg.cookies, message, cfg.mid)) {
			cfg.login_status = true;
			std::string newRoomId, newCsrfToken;
			if (Bili::BiliApi::getRoomIdAndCsrf(cfg.cookies, newRoomId, newCsrfToken, message)) {
				cfg.room_id = newRoomId;
				cfg.csrf_token = newCsrfToken;
				m_config.save();
			}
		}
	}

	updateLoginStatus();

	bool isLive = false;
	std::string message;
	if (queryLiveStatus(isLive, message)) {
		setStreamingState(isLive);
	} else {
		setStreamingState(cfg.streaming);
		if (!message.empty()) {
			obs_log(LOG_WARNING, "Failed to query Bilibili live status on startup: %s", message.c_str());
		}
	}

	obs_frontend_add_event_callback(&BilibiliStreamPlugin::frontendEventCallback, this);
	m_frontendCallbackRegistered = true;
}

BilibiliStreamPlugin::~BilibiliStreamPlugin()
{
	if (m_frontendCallbackRegistered) {
		obs_frontend_remove_event_callback(&BilibiliStreamPlugin::frontendEventCallback, this);
		m_frontendCallbackRegistered = false;
	}

	obs_log(LOG_DEBUG, "Released BilibiliStreamPlugin resources");
}

void BilibiliStreamPlugin::frontendEventCallback(enum obs_frontend_event event, void *private_data)
{
	auto *plugin = static_cast<BilibiliStreamPlugin *>(private_data);
	if (plugin) {
		plugin->handleFrontendEvent(event);
	}
}

void BilibiliStreamPlugin::handleFrontendEvent(enum obs_frontend_event event)
{
	if (event != OBS_FRONTEND_EVENT_STREAMING_STOPPED) {
		return;
	}

	auto &cfg = m_config.config();
	bool shouldStopLive = cfg.streaming;
	bool isLive = false;
	std::string statusMessage;

	if (queryLiveStatus(isLive, statusMessage)) {
		shouldStopLive = isLive;
		setStreamingState(isLive);
	} else if (!statusMessage.empty()) {
		obs_log(LOG_WARNING, "Failed to query Bilibili live status after OBS stop event: %s",
			statusMessage.c_str());
	}

	if (!shouldStopLive) {
		obs_log(LOG_INFO, "OBS streaming stopped; Bilibili live is already inactive");
		return;
	}

	if (cfg.room_id.empty() || cfg.csrf_token.empty() || cfg.cookies.empty()) {
		obs_log(LOG_WARNING, "OBS streaming stopped but Bilibili stopLive was skipped because config is incomplete");
		return;
	}

	std::string stopMessage;
	if (Bili::BiliApi::stopLive(cfg, stopMessage)) {
		obs_log(LOG_INFO, "OBS streaming stopped; synchronized Bilibili stopLive successfully");
		setStreamingState(false);
		m_config.save();
	} else {
		obs_log(LOG_ERROR, "OBS streaming stopped; failed to synchronize Bilibili stopLive: %s",
			stopMessage.c_str());
	}
}

void BilibiliStreamPlugin::updateLoginStatus()
{
	auto &cfg = m_config.config();
	m_menu->actions().loginStatus->setText(cfg.login_status ? QStringLiteral("登录状态: 已登录")
							 : QStringLiteral("登录状态: 未登录"));
	m_menu->actions().loginStatus->setChecked(cfg.login_status);
}

void BilibiliStreamPlugin::openLiveRoom()
{
	auto &cfg = m_config.config();
	if (cfg.room_id.empty()) {
		UI::DialogFactory::message(QStringLiteral("room_id 为空，请先登录或更新直播间信息"), QStringLiteral("消息"));
		return;
	}
	const QString url = QString("https://live.bilibili.com/%1").arg(QString::fromStdString(cfg.room_id));
	if (!QDesktopServices::openUrl(QUrl(url))) {
		UI::DialogFactory::message(QStringLiteral("无法打开浏览器，请手动访问：\n") + url, QStringLiteral("消息"));
	}
}

void BilibiliStreamPlugin::setStreamingState(bool streaming)
{
	auto &cfg = m_config.config();
	cfg.streaming = streaming;
	m_menu->actions().streamToggle->setText(streaming ? QStringLiteral("停止直播") : QStringLiteral("开始直播"));
}

bool BilibiliStreamPlugin::queryLiveStatus(bool &is_live, std::string &message)
{
	auto &cfg = m_config.config();
	if (cfg.cookies.empty()) {
		message = "Bilibili cookies are empty";
		return false;
	}

	return Bili::BiliApi::getLiveStatus(cfg, is_live, message);
}

bool BilibiliStreamPlugin::queryCurrentRoomInfo(Bili::RoomInfo &roomInfo, std::string &message)
{
	auto &cfg = m_config.config();
	if (cfg.cookies.empty()) {
		message = "Bilibili cookies are empty";
		return false;
	}
	if (cfg.room_id.empty()) {
		message = "room_id is empty";
		return false;
	}

	return Bili::BiliApi::getCurrentRoomInfo(cfg, roomInfo, message);
}

bool BilibiliStreamPlugin::prepareStartConfig(Bili::Config &startConfig, std::string &message)
{
	Bili::RoomInfo currentRoomInfo;
	bool hasCurrentRoomInfo = queryCurrentRoomInfo(currentRoomInfo, message);
	if (!hasCurrentRoomInfo && !message.empty()) {
		obs_log(LOG_WARNING, "Failed to query current room info before start live: %s", message.c_str());
	}

	auto &cfg = m_config.config();
	startConfig = cfg;
	if (!hasCurrentRoomInfo) {
		message = "无法读取当前直播间信息，无法直接开播，请先更新直播间信息";
		return false;
	}
	if (currentRoomInfo.area_id <= 0) {
		message = "未能读取当前直播间分区，无法直接开播，请先更新直播间信息";
		return false;
	}

	startConfig.area_id = currentRoomInfo.area_id;
	startConfig.part_id = currentRoomInfo.part_id;
	return true;
}

bool BilibiliStreamPlugin::tryStartLive(const Bili::Config &startConfig, std::string &message, std::string &faceQr)
{
	auto retryConfig = startConfig;
	auto &cfg = m_config.config();
	std::string rtmpAddr, rtmpCode;
	if (Bili::BiliApi::startLive(retryConfig, rtmpAddr, rtmpCode, message, faceQr, cfg.mid)) {
		handleStartLiveSuccess(rtmpAddr, rtmpCode);
		return true;
	}
	return false;
}

void BilibiliStreamPlugin::handleStartLiveSuccess(const std::string &rtmpAddr, const std::string &rtmpCode)
{
	auto &cfg = m_config.config();
	setStreamingState(true);
	cfg.rtmp_addr = rtmpAddr;
	cfg.rtmp_code = rtmpCode;
	m_config.save();
	UI::DialogFactory::streamStarted((QWidget *)obs_frontend_get_main_window(), rtmpAddr, rtmpCode);
}

void BilibiliStreamPlugin::handleFaceAuthRequired(const Bili::Config &startConfig, const std::string &faceUrl)
{
	auto parent = (QWidget *)obs_frontend_get_main_window();

	UI::DialogFactory::faceAuth(
		parent, faceUrl,
		[this, startConfig](std::string &statusMessage) -> bool {
			bool isIdentified = false;
			if (!Bili::BiliApi::isUserIdentifiedByFaceAuth(startConfig, isIdentified, statusMessage)) {
				if (statusMessage.empty()) {
					statusMessage = "查询人脸认证状态失败，插件会继续自动轮询...";
				}
				return false;
			}

			if (isIdentified) {
				statusMessage = "人脸认证已完成，准备自动开播...";
				return true;
			}

			statusMessage = "仍在等待人脸认证完成...";
			return false;
		},
		[this, startConfig]() {
			QTimer::singleShot(0, this, [this, startConfig]() {
				std::string statusMessage;
				std::string faceQr;
				if (!tryStartLive(startConfig, statusMessage, faceQr)) {
					if (!faceQr.empty()) {
						handleFaceAuthRequired(startConfig, faceQr);
					} else {
						UI::DialogFactory::message(QString::fromUtf8(statusMessage.c_str()), QStringLiteral("开播失败"));
					}
				}
			});
		});
}

void BilibiliStreamPlugin::onOpenRoom()
{
	openLiveRoom();
}

void BilibiliStreamPlugin::onScanQrcode()
{
	auto &cfg = m_config.config();
	std::string qrData, qrKey, message;
	if (!Bili::BiliApi::getQrCode(cfg.cookies, qrData, qrKey, message)) {
		obs_log(LOG_ERROR, "Failed to get QR code");
		UI::DialogFactory::message(QString::fromUtf8(message), QStringLiteral("消息"));
		return;
	}

	auto parent = (QWidget *)obs_frontend_get_main_window();
	UI::DialogFactory::qrLogin(parent, qrData, qrKey, [this, &cfg](const std::string &cookies) {
		std::string msg;
		cfg.cookies = cookies;
		cfg.login_status = Bili::BiliApi::checkLoginStatus(cfg.cookies, msg, cfg.mid);
		updateLoginStatus();

		std::string newRoomId, newCsrfToken;
		if (Bili::BiliApi::getRoomIdAndCsrf(cfg.cookies, newRoomId, newCsrfToken, msg)) {
			cfg.room_id = newRoomId;
			cfg.csrf_token = newCsrfToken;
		}

		bool isLive = false;
		if (queryLiveStatus(isLive, msg)) {
			setStreamingState(isLive);
		}

		m_config.save();
	});
}

void BilibiliStreamPlugin::onStreamToggle()
{
	auto &cfg = m_config.config();
	std::string message;

	if (cfg.streaming) {
		if (Bili::BiliApi::stopLive(cfg, message)) {
			setStreamingState(false);
			m_config.save();
			UI::DialogFactory::message(QStringLiteral("直播已停止"), QStringLiteral("消息"));
		} else {
			UI::DialogFactory::message(QString::fromUtf8(message), QStringLiteral("消息"));
		}
		return;
	}

	Bili::Config startConfig = cfg;
	if (!prepareStartConfig(startConfig, message)) {
		UI::DialogFactory::message(QString::fromUtf8(message.c_str()), QStringLiteral("消息"));
		return;
	}

	std::string rtmpAddr, rtmpCode, faceQr;
	if (tryStartLive(startConfig, message, faceQr)) {
		return;
	} else {
		if (!faceQr.empty()) {
			handleFaceAuthRequired(startConfig, faceQr);
		} else {
			UI::DialogFactory::message(QString::fromUtf8(message.c_str()), QStringLiteral("开播失败"));
		}
	}
}

void BilibiliStreamPlugin::onUpdateRoomInfo()
{
	auto &cfg = m_config.config();
	auto parent = (QWidget *)obs_frontend_get_main_window();

	Bili::RoomInfo currentRoomInfo;
	std::string roomInfoMessage;
	const bool hasCurrentRoomInfo = queryCurrentRoomInfo(currentRoomInfo, roomInfoMessage);
	if (!hasCurrentRoomInfo && !roomInfoMessage.empty()) {
		obs_log(LOG_WARNING, "Failed to query current room info before editing: %s", roomInfoMessage.c_str());
	}

	const std::string dialogTitle = hasCurrentRoomInfo ? currentRoomInfo.title : cfg.title;
	const int dialogAreaId = hasCurrentRoomInfo ? currentRoomInfo.area_id : cfg.area_id;
	const int dialogPartId = hasCurrentRoomInfo ? currentRoomInfo.part_id : cfg.part_id;

	UI::DialogFactory::roomSettings(parent, cfg.room_id, dialogTitle, dialogAreaId, dialogPartId,
					[this, &cfg, currentRoomInfo, hasCurrentRoomInfo](
						const std::string &title, int areaId, int partId) {
						const std::string baseTitle = hasCurrentRoomInfo ? currentRoomInfo.title : cfg.title;
						const int baseAreaId = hasCurrentRoomInfo ? currentRoomInfo.area_id : cfg.area_id;
						const int basePartId = hasCurrentRoomInfo ? currentRoomInfo.part_id : cfg.part_id;

						QStringList successMessages;
						std::string message;

						if (!title.empty() && title != baseTitle) {
							if (!Bili::BiliApi::updateRoomInfo(cfg, title, message)) {
								UI::DialogFactory::message(QString::fromUtf8(message), QStringLiteral("消息"));
								return;
							}
							cfg.title = title;
							successMessages.append(QStringLiteral("直播间标题已更新"));
						} else if (!title.empty()) {
							cfg.title = title;
						}

						if (areaId > 0 && (areaId != baseAreaId || partId != basePartId)) {
							if (!Bili::BiliApi::updateRoomArea(cfg, areaId, message)) {
								UI::DialogFactory::message(QString::fromUtf8(message), QStringLiteral("消息"));
								return;
							}
							cfg.area_id = areaId;
							cfg.part_id = partId;
							successMessages.append(QStringLiteral("直播间分区已更新"));
						} else if (areaId > 0) {
							cfg.area_id = areaId;
							cfg.part_id = partId;
						}

						m_config.save();

						if (successMessages.empty()) {
							UI::DialogFactory::message(QStringLiteral("直播间信息没有变化"), QStringLiteral("消息"));
							return;
						}

						UI::DialogFactory::message(successMessages.join(QStringLiteral("\n")), QStringLiteral("消息"));
					});
}

static BilibiliStreamPlugin *plugin = nullptr;

bool obs_module_load(void)
{
	Bili::BiliApi::init();
	auto mainWindow = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (!mainWindow)
		return false;
	plugin = new BilibiliStreamPlugin(mainWindow);
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
