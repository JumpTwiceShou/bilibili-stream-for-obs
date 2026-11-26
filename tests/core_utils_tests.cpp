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
	const auto release = [](const char *tag, bool draft = false, bool prerelease = false) {
		return json11::Json::object{{"tag_name", tag}, {"draft", draft}, {"prerelease", prerelease}};
	};
	expect(Core::newerReleaseVersion(release("v1.3.10"), "1.3.9") == "1.3.10" &&
		       Core::newerReleaseVersion(release("2.0.0"), "1.9.9") == "2.0.0",
	       "release versions are compared numerically and accept a v prefix");
	for (const auto *tag : {"1.3.3", "1.3.4", "1.4", "2.0.0-beta.1", "2.0.0+build", "not-a-version"})
		expect(Core::newerReleaseVersion(release(tag), "1.3.4").empty(),
		       "same, older, malformed and suffixed tags do not prompt for an update");
	expect(Core::newerReleaseVersion(release("2.0.0", true), "1.3.4").empty() &&
		       Core::newerReleaseVersion(release("2.0.0", false, true), "1.3.4").empty() &&
		       Core::newerReleaseVersion(json11::Json::object{{"tag_name", "2.0.0"}}, "1.3.4").empty(),
	       "drafts, prereleases and incomplete release responses do not prompt");
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

	Core::FaceAuthRetryPolicy faceRetry;
	expect(faceRetry.shouldRetry(true, true, 0), "first verified reply can immediately attempt start");
	faceRetry.deferRetry(100);
	expect(!faceRetry.shouldRetry(true, true, 5099) && faceRetry.shouldRetry(true, true, 5100),
	       "persistent positive retries after five seconds without needing a pending response");
	expect(!faceRetry.shouldRetry(false, true, 5100) && !faceRetry.shouldRetry(true, false, 5100),
	       "query errors and unverified replies never authorize automatic start");
	faceRetry.deferRetry(5100);
	expect(!faceRetry.shouldRetry(true, true, 15099) && faceRetry.shouldRetry(true, true, 15100),
	       "a second rejected attempt backs off ten seconds");
	faceRetry.deferRetry(15100);
	expect(!faceRetry.shouldRetry(true, true, 35099) && faceRetry.shouldRetry(true, true, 35100),
	       "a third rejected attempt backs off twenty seconds");
	faceRetry.deferRetry(35100);
	expect(!faceRetry.shouldRetry(true, true, 65099) && faceRetry.shouldRetry(true, true, 65100),
	       "start retry backoff caps at thirty seconds");
	faceRetry.deferRetry(65100);
	expect(!faceRetry.shouldRetry(true, true, 95099) && faceRetry.shouldRetry(true, true, 95100),
	       "later rejected attempts keep the bounded thirty second cooldown");
	expect(faceRetry.expired(600000) && !faceRetry.shouldRetry(true, true, 600000),
	       "automatic retry ends at the ten minute verification deadline");
	Core::FaceAuthRetryPolicy persistentPositive;
	int attempts = 0;
	for (std::int64_t elapsed = 0; elapsed <= 610000; elapsed += 1500) {
		if (persistentPositive.shouldRetry(true, true, elapsed)) {
			++attempts;
			persistentPositive.deferRetry(elapsed);
		}
	}
	expect(attempts > 1 && attempts <= 24,
	       "persistent stale success cannot stall forever or issue a start on every status poll");

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
	expect(missingRoomControls.refreshRoomEnabled && !busyControls.refreshRoomEnabled &&
		       !Core::controlState(false, false, false, false).refreshRoomEnabled,
	       "manual room refresh is available with saved login even when the room is missing");
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
