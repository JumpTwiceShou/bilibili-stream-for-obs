#include "account_state.hpp"
#include "api_utils.hpp"
#include <utility>

namespace Core {
namespace {
void clearRoomState(Bili::Config &config, bool keepRoutePreference = false)
{
	config.room_id.clear();
	config.title.clear();
	config.part_id = config.area_id = 0;
	config.last_rtmp_address.clear();
	config.last_rtmp_key.clear();
	if (!keepRoutePreference)
		config.selected_rtmp_address.clear();
	config.managed_session = false;
}
} // namespace

std::string accountIdentity(const Bili::Config &config)
{
	return config.mid.empty() ? extractCookieValue(config.cookies, "DedeUserID") : config.mid;
}

Bili::Config prepareAccountLogin(const Bili::Config &previous, const std::string &cookies)
{
	Bili::Config config = previous;
	const auto incoming = extractCookieValue(cookies, "DedeUserID");
	const bool sameAccount = !incoming.empty() && incoming == accountIdentity(previous);
	if (!sameAccount)
		clearRoomState(config);
	else if (!config.managed_session) {
		// A route preference may survive reauthentication; an old publish key must not.
		config.last_rtmp_address.clear();
		config.last_rtmp_key.clear();
	}
	config.cookies = cookies;
	config.csrf_token = extractCookieValue(cookies, "bili_jct");
	config.mid = incoming;
	config.login_status = false;
	return config;
}

AccountResult queryAccountState(Bili::Config config)
{
	AccountResult result;
	result.config = std::move(config);
	bool loggedIn = false;
	std::string verifiedId, message;
	if (!Bili::BiliApi::checkLoginStatus(result.config.cookies, loggedIn, message, verifiedId)) {
		result.message = message;
		return result;
	}
	result.requestSucceeded = true;
	const auto cookieId = extractCookieValue(result.config.cookies, "DedeUserID");
	const bool identityMismatch = loggedIn && (verifiedId.empty() || verifiedId == "0" || verifiedId != cookieId);
	if (identityMismatch) {
		loggedIn = false;
		result.message = "登录身份信息不一致，请重新扫码登录";
	}
	result.loggedIn = loggedIn;
	if (!loggedIn) {
		const auto previousId = accountIdentity(result.config);
		clearRoomState(result.config, !identityMismatch);
		result.config.cookies.clear();
		result.config.csrf_token.clear();
		// Identity and a public route preference are not login/publish credentials.
		result.config.mid = identityMismatch ? std::string() : previousId;
		result.config.login_status = false;
		if (result.message.empty())
			result.message = "Bilibili 登录已失效，请重新扫码登录";
		return result;
	}
	if (verifiedId != accountIdentity(result.config))
		clearRoomState(result.config);
	result.config.mid = verifiedId;
	result.config.login_status = true;
	std::string roomId, csrfToken;
	if (!Bili::BiliApi::getRoomIdAndCsrf(result.config.cookies, roomId, csrfToken, message)) {
		// Retain a known same-account managed room so it can still be stopped after a transient lookup failure.
		if (!result.config.managed_session)
			clearRoomState(result.config, true);
		result.message = "登录成功，但未能获取直播间信息。\n" + message;
		return result;
	}
	if (!result.config.room_id.empty() && result.config.room_id != roomId)
		clearRoomState(result.config, true);
	result.config.room_id = roomId;
	result.config.csrf_token = csrfToken;
	result.roomAvailable = true;
	if (Bili::BiliApi::getLiveStatus(result.config, result.isLive, message)) {
		result.liveStatusKnown = true;
		if (!result.isLive)
			result.config.managed_session = false;
	} else
		result.message = "登录成功，但未能确认直播间状态。\n" + message;
	return result;
}

AccountApplyDecision applyAccountRefresh(Bili::Config &current, const AccountResult &result, bool candidateLogin)
{
	if (!result.requestSucceeded || (candidateLogin && !result.loggedIn))
		return AccountApplyDecision::Failed;
	if (current.managed_session && result.loggedIn && accountIdentity(current) != accountIdentity(result.config))
		return AccountApplyDecision::ManagedAccountChange;
	const bool stillManaged = current.managed_session;
	current = result.config;
	current.managed_session = stillManaged && current.managed_session;
	return AccountApplyDecision::Applied;
}
} // namespace Core
