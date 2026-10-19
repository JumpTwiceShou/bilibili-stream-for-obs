#pragma once

#include <QString>
#include <QWidget>
#include <functional>
#include <string>

#include "bilibili_api.hpp"
#include "core/api_utils.hpp"

namespace UI {
struct RoomChanges {
	std::string baseTitle, title;
	int baseAreaId = 0, basePartId = 0, areaId = 0, partId = 0;
};
struct RoomApplyResult {
	bool titleUpdated = false, areaUpdated = false;
	std::string message;
};
using RoomApplyCallback = std::function<void(RoomApplyResult)>;
using RoomApplyRequest = std::function<void(const RoomChanges &, RoomApplyCallback)>;

class DialogFactory {
public:
	using QrPollCallback =
		std::function<void(Bili::BiliApi::QrLoginResult result, std::string cookies, std::string message)>;
	using QrPollRequest = std::function<void(QrPollCallback callback)>;
	using FacePollCallback = std::function<void(bool requestSucceeded, bool identified, std::string statusMessage)>;
	using FacePollRequest = std::function<void(FacePollCallback callback)>;
	using FaceRetryCallback = std::function<void(Core::FaceAuthRetryDecision decision, std::string faceUrl,
						     std::string statusMessage)>;
	using FaceRetryRequest = std::function<void(FaceRetryCallback callback)>;

	static void message(const QString &msg, const QString &title, QWidget *parent = nullptr);
	static bool confirm(const QString &msg, const QString &title, QWidget *parent = nullptr);
	static void qrLogin(QWidget *parent, const std::string &qrData, QrPollRequest requestPoll,
			    std::function<void(const std::string &cookies)> onSuccess);
	static bool faceAuth(QWidget *parent, const std::string &faceUrl, FacePollRequest requestPoll,
			     FaceRetryRequest requestRetry);
	static void roomSettings(QWidget *parent, const std::string &roomUrl, const std::string &currentTitle,
				 int currentAreaId, int currentPartId, const json11::Json &partitionData,
				 RoomApplyRequest onApply);
};
} // namespace UI
