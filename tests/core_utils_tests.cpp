#include <iostream>
#include <string>

#include "core/api_utils.hpp"
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
