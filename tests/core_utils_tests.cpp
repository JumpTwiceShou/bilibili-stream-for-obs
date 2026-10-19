#include <iostream>
#include <string>

#include "core/api_utils.hpp"
#include "core/login_utils.hpp"
#include "core/secret_store.hpp"

namespace {
int failures = 0;

void expect(bool condition, const char *description)
{
	if (condition)
		return;
	std::cerr << "FAIL: " << description << '\n';
	++failures;
}
} // namespace

int main()
{
	int apiCode = 0;
	expect(Core::apiResponseCode(json11::Json::object{{"code", -101}}, apiCode) && apiCode == -101,
	       "negative API error codes are valid explicit failures");
	expect(!Core::apiResponseCode(json11::Json::object{}, apiCode) &&
		       !Core::apiResponseCode(json11::Json::object{{"code", false}}, apiCode),
	       "missing and boolean status codes are not zero success");
	expect(Core::apiErrorMessage(json11::Json::object{{"code", -1}, {"message", " "}, {"msg", "fallback"}},
				     "test") == "test失败: fallback",
	       "empty message falls back to msg");
	expect(Core::apiErrorMessage(json11::Json::object{{"code", -500}}, "test").find("-500") != std::string::npos,
	       "missing error text retains the code");
	expect(Core::trustedLoginUrl("https://passport.biligame.com/x/passport-login/web/crossDomain?ticket=fixture") &&
		       Core::trustedLoginUrl("https://passport.bilibili.com/login"),
	       "known HTTPS login hosts are allowed");
	for (const auto *url : {"http://passport.biligame.com/login", "https://passport.biligame.com.attacker.invalid/",
				"https://user:password@passport.bilibili.com/", "https://passport.bilibili.com:444/",
				"file:///tmp/login", "https://www.bilibili.com/#fragment"})
		expect(!Core::trustedLoginUrl(url),
		       "non-HTTPS, untrusted hosts, userinfo and unusual ports are rejected");
	expect(Core::mergeLoginCookies("SESSDATA=old; bili_jct=csrf", "SESSDATA=new; DedeUserID=42") ==
		       "SESSDATA=new; bili_jct=csrf; DedeUserID=42",
	       "last cookie values win across ticket redirects");
	expect(!Core::completeLoginCookies("SESSDATA=session; bili_jct=csrf; DedeUserID__ckMd5=42"),
	       "lookalike cookie names do not complete login");
	expect(!Core::completeLoginCookies("SESSDATA=bad\x01value; bili_jct=csrf; DedeUserID=42"),
	       "control characters cannot enter accepted login credentials");
	const auto longMessage =
		Core::apiErrorMessage(json11::Json::object{{"code", -1}, {"msg", std::string(800, 'x')}}, "test");
	expect(longMessage.size() < 550, "error text is bounded instead of filling the dialog");
	expect(Core::extractCookieValue("foo=1; DedeUserID=42; bili_jct=abc", "DedeUserID") == "42",
	       "extracts an exact cookie name");
	expect(Core::extractCookieValue("NotDedeUserID=99", "DedeUserID").empty(),
	       "does not match a cookie-name suffix");
	expect(Core::encodeFormComponent("A&B +中文") == "A%26B%20%2B%E4%B8%AD%E6%96%87",
	       "encodes reserved and UTF-8 form bytes");
	expect(Core::buildFormBody({{"title", "A&B"}, {"area", "86"}}) == "title=A%26B&area=86",
	       "builds an encoded form body");

	int value = 0;
	expect(Core::jsonInteger(json11::Json("86"), value, 1) && value == 86, "parses a numeric string");
	expect(Core::jsonInteger(json11::Json(86), value, 1) && value == 86, "parses an integer JSON number");
	expect(!Core::jsonInteger(json11::Json("8x"), value, 1), "rejects a malformed numeric string");
	expect(!Core::jsonInteger(json11::Json(1.5), value, 1), "rejects a fractional JSON number");
	expect(!Core::jsonInteger(json11::Json(), value, 1), "rejects a missing value without throwing");
	expect(Core::isIpFollowerRestriction("当前IP开播要求粉丝数大于100"), "IP/follower restriction unlocks proxy");
	expect(Core::isIpFollowerRestriction("Current IP requires at least 100 followers"),
	       "English restriction unlocks proxy");
	expect(!Core::isIpFollowerRestriction("粉丝数不足") && !Core::isIpFollowerRestriction("当前IP无效"),
	       "unrelated errors keep entry hidden");
	expect(!Core::isIpFollowerRestriction("subscription follower count"), "IP must be an independent token");
	bool identified = true;
	for (const auto &flag : {json11::Json(true), json11::Json(1), json11::Json("1")}) {
		expect(Core::faceAuthIdentified(
			       json11::Json::object{{"code", 0},
						    {"data", json11::Json::object{{"is_identified", flag}}}},
			       identified) &&
			       identified,
		       "accepts explicit boolean and binary numeric face success");
	}
	for (const auto &flag : {json11::Json(false), json11::Json(0), json11::Json("0")}) {
		expect(Core::faceAuthIdentified(
			       json11::Json::object{{"code", 0},
						    {"data", json11::Json::object{{"is_identified", flag}}}},
			       identified) &&
			       !identified,
		       "accepts explicit face pending state");
	}
	for (const auto &flag : {json11::Json(), json11::Json(2), json11::Json("true"), json11::Json(-1)}) {
		expect(!Core::faceAuthIdentified(
			       json11::Json::object{{"code", 0},
						    {"data", json11::Json::object{{"is_identified", flag}}}},
			       identified) &&
			       !identified,
		       "unknown face state must never authorize start");
	}
	expect(!Core::faceAuthIdentified(json11::Json::object{{"data", json11::Json::object{{"is_identified", true}}}},
					 identified),
	       "missing API status cannot authorize start");
	expect(Core::classifyFaceAuthRetry(true, false) == Core::FaceAuthRetryDecision::Started,
	       "completes face authentication only after start-live succeeds");
	expect(Core::classifyFaceAuthRetry(false, true) == Core::FaceAuthRetryDecision::ContinueWaiting,
	       "keeps one face-auth dialog open when start-live still requires verification");
	expect(Core::classifyFaceAuthRetry(false, false) == Core::FaceAuthRetryDecision::Failed,
	       "surfaces a terminal start-live failure after face authentication");

	expect(!Core::shouldStopManagedSession(false, true, true), "does not stop an unmanaged live session");
	expect(!Core::shouldStopManagedSession(true, true, false), "does not stop an already inactive managed session");
	expect(Core::shouldStopManagedSession(true, true, true), "stops an active managed session");
	expect(Core::shouldStopManagedSession(true, false, false), "fails safe when managed-session status is unknown");

	const Core::ControlState loggedInControls = Core::controlState(false, true, true, true);
	expect(loggedInControls.scanQrEnabled && loggedInControls.logoutEnabled &&
		       loggedInControls.streamToggleEnabled && loggedInControls.updateRoomEnabled &&
		       loggedInControls.openRoomEnabled,
	       "enables authenticated room controls after an idle account refresh");
	const Core::ControlState offlineControls = Core::controlState(false, false, false, true);
	expect(offlineControls.logoutEnabled && !offlineControls.streamToggleEnabled,
	       "keeps logout available when saved credentials exist but login status is unavailable");
	const Core::ControlState busyControls = Core::controlState(true, true, true, true);
	const auto missingRoomControls = Core::controlState(false, true, false, true);
	expect(!missingRoomControls.streamToggleEnabled && !missingRoomControls.updateRoomEnabled &&
		       !missingRoomControls.openRoomEnabled && missingRoomControls.scanQrEnabled,
	       "room lookup failure disables room actions while preserving login recovery");
	expect(!Core::controlState(false, false, true, true).openRoomEnabled,
	       "stale saved room data cannot enable opening a signed-out account's room");
	expect(!busyControls.scanQrEnabled && !busyControls.logoutEnabled && !busyControls.streamToggleEnabled &&
		       !busyControls.updateRoomEnabled && !busyControls.openRoomEnabled,
	       "disables all controls while an operation is in flight");

	std::string protectedSecret;
	std::string restoredSecret;
	std::string message;
	expect(Core::protectSecret("session-cookie", protectedSecret, message), "protects a login secret");
	expect(protectedSecret.find("session-cookie") == std::string::npos, "does not store the secret verbatim");
	expect(Core::unprotectSecret(protectedSecret, restoredSecret, message) && restoredSecret == "session-cookie",
	       "round-trips a protected login secret");

	if (failures == 0)
		std::cout << "All core utility tests passed\n";
	return failures == 0 ? 0 : 1;
}
