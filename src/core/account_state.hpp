#pragma once
#include "bilibili_api.hpp"

namespace Core {
struct AccountResult {
	Bili::Config config;
	bool requestSucceeded = false;
	bool loggedIn = false;
	bool roomAvailable = false;
	bool liveStatusKnown = false;
	bool isLive = false;
	std::string message;
};
std::string accountIdentity(const Bili::Config &config);
Bili::Config prepareAccountLogin(const Bili::Config &previous, const std::string &cookies);
AccountResult queryAccountState(Bili::Config config);
enum class AccountApplyDecision { Applied, Failed, ManagedAccountChange };
AccountApplyDecision applyAccountRefresh(Bili::Config &current, const AccountResult &result, bool candidateLogin);
} // namespace Core
