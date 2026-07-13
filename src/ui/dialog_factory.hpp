#pragma once

#include <QString>
#include <QWidget>
#include <functional>
#include <string>

#include "bilibili_api.hpp"

namespace UI {
class DialogFactory {
public:
	using QrPollCallback =
		std::function<void(Bili::BiliApi::QrLoginResult result, std::string cookies, std::string message)>;
	using QrPollRequest = std::function<void(QrPollCallback callback)>;
	using FacePollCallback = std::function<void(bool requestSucceeded, bool identified, std::string statusMessage)>;
	using FacePollRequest = std::function<void(FacePollCallback callback)>;

	static void message(const QString &msg, const QString &title, QWidget *parent = nullptr);
	static bool confirm(const QString &msg, const QString &title, QWidget *parent = nullptr);
	static void qrLogin(QWidget *parent, const std::string &qrData, QrPollRequest requestPoll,
			    std::function<void(const std::string &cookies)> onSuccess);
	static void streamStarted(QWidget *parent, const std::string &rtmpAddr, const std::string &rtmpCode);
	static void faceAuth(QWidget *parent, const std::string &faceUrl, FacePollRequest requestPoll,
			     std::function<void()> onVerified);
	static void roomSettings(QWidget *parent, const std::string &roomUrl, const std::string &currentTitle,
				 int currentAreaId, int currentPartId, const json11::Json &partitionData,
				 std::function<void(const std::string &title, int areaId, int partId)> onApply);
};
} // namespace UI
