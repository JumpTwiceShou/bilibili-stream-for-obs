#pragma once

#include <string>
#include <utility>
#include <vector>

#include "json11/json11.hpp"

namespace Core {
struct ControlState {
	bool scanQrEnabled = false;
	bool logoutEnabled = false;
	bool streamToggleEnabled = false;
	bool updateRoomEnabled = false;
	bool openRoomEnabled = false;
};

enum class FaceAuthRetryDecision { Started, ContinueWaiting, Failed };

std::string extractCookieValue(const std::string &cookies, const std::string &key);
std::string encodeFormComponent(const std::string &value);
std::string buildFormBody(const std::vector<std::pair<std::string, std::string>> &params);
bool jsonInteger(const json11::Json &value, int &result, int minimum = 0);
bool apiResponseCode(const json11::Json &response, int &code);
std::string apiErrorMessage(const json11::Json &response, const std::string &action);
bool faceAuthIdentified(const json11::Json &response, bool &identified);
bool isIpFollowerRestriction(const std::string &message);
FaceAuthRetryDecision classifyFaceAuthRetry(bool started, bool faceAuthRequired);
bool shouldStopManagedSession(bool managedSession, bool statusKnown, bool isLive);
ControlState controlState(bool busy, bool loggedIn, bool hasRoomId, bool hasCredentials);
} // namespace Core
