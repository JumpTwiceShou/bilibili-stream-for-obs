#include "bilibili_api.hpp"
#include "core/api_utils.hpp"
#include "core/account_state.hpp"
#include "core/login_utils.hpp"
#include "http_client.hpp"
#include <QByteArray>
#include <QCoreApplication>
#include <deque>
#include <iostream>

namespace {
int failures = 0;
struct Request {
	std::string method, url, body;
	std::vector<std::string> headers;
};
std::deque<Http::HttpResponse> replies;
std::vector<Request> requests;
void check(bool condition, const char *description)
{
	if (!condition) {
		++failures;
		std::cerr << "FAIL: " << description << '\n';
	}
}
Http::HttpResponse reply(std::string data, std::string cookies = {})
{
	Http::HttpResponse response;
	response.status = 200;
	response.data = std::move(data);
	response.cookies = std::move(cookies);
	return response;
}
Http::HttpResponse take(const char *method, const std::string &url, std::string body = {})
{
	requests.push_back({method, url, std::move(body)});
	if (replies.empty()) {
		check(false, "API issued an unexpected extra request");
		return {};
	}
	auto response = std::move(replies.front());
	replies.pop_front();
	return response;
}
void reset(std::initializer_list<Http::HttpResponse> responses)
{
	check(replies.empty(), "all expected API requests were consumed");
	replies = responses;
	requests.clear();
}
} // namespace

namespace Http {
bool HttpClient::init()
{
	return true;
}
void HttpClient::cleanup() {}
HttpResponse HttpClient::get(const std::string &url, const std::vector<std::string> &headers, long)
{
	auto response = take("GET", url);
	requests.back().headers = headers;
	return response;
}
HttpResponse HttpClient::getLoginTicket(const std::string &url, const std::vector<std::string> &, long)
{
	return take("TICKET", url);
}
HttpResponse HttpClient::post(const std::string &url, const std::string &body, const std::vector<std::string> &headers,
			      long)
{
	auto response = take("POST", url, body);
	requests.back().headers = headers;
	return response;
}
HttpResponse HttpClient::postWithoutRedirects(const std::string &url, const std::string &body,
					      const std::vector<std::string> &headers, long)
{
	auto response = take("POST-no-redirect", url, body);
	requests.back().headers = headers;
	return response;
}
} // namespace Http

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	using Api = Bili::BiliApi;
	const std::string complete = "SESSDATA=fixture-session; bili_jct=fixture-csrf; DedeUserID=42";
	const std::string poll =
		R"({"code":0,"data":{"code":0,"url":"https://passport.biligame.com/x/passport-login/web/crossDomain?ticket=fixture-ticket"}})";
	std::string cookies, message;
	reset({reply(poll, complete)});
	check(Api::qrLogin("fixture-key", cookies, message) == Api::QrLoginResult::Success && cookies == complete &&
		      requests.size() == 1,
	      "complete response cookies do not need a ticket exchange");
	reset({reply(poll, "sid=ancillary; SESSDATA=old-session"), reply("", complete)});
	check(Api::qrLogin("fixture-key", cookies, message) == Api::QrLoginResult::Success && cookies == complete &&
		      requests.size() == 2 && requests.back().method == "TICKET",
	      "new crossDomain flow exchanges a ticket even when partial cookies were returned");
	reset({reply(
		R"({"code":0,"data":{"code":0,"url":"https://www.bilibili.com/?SESSDATA=legacy%2Cvalue&bili_jct=legacy-csrf&DedeUserID=42"}})")});
	check(Api::qrLogin("fixture-key", cookies, message) == Api::QrLoginResult::Success && requests.size() == 1 &&
		      Core::extractCookieValue(cookies, "SESSDATA") == "legacy%2Cvalue",
	      "legacy query cookies retain their encoded values and exact query boundaries");
	for (const auto *bad :
	     {R"({"code":-101,"data":{"code":0}})", R"({"data":{"code":0}})", R"({"code":0,"data":{}})",
	      R"({"code":0,"data":{"code":null}})", R"({"code":0,"data":{"code":false}})",
	      R"({"code":0.5,"data":{"code":0}})", "<html>not JSON</html>"}) {
		reset({reply(bad, complete)});
		cookies = "stale";
		check(Api::qrLogin("fixture-key", cookies, message) == Api::QrLoginResult::Error && cookies.empty() &&
			      !message.empty(),
		      "bad/missing QR status cannot succeed or retain stale credentials");
	}
	for (const auto &[code, expected] :
	     std::vector<std::pair<int, Api::QrLoginResult>>{{86038, Api::QrLoginResult::Expired},
							     {86090, Api::QrLoginResult::Scanned},
							     {86101, Api::QrLoginResult::Pending}}) {
		reset({reply("{\"code\":0,\"data\":{\"code\":" + std::to_string(code) + "}}")});
		check(Api::qrLogin("fixture-key", cookies, message) == expected && requests.size() == 1,
		      "QR pending/scanned/expired never initiate ticket requests");
	}
	reset({reply(
		R"({"code":0,"data":{"code":0,"url":"https://passport.biligame.com.attacker.invalid/crossDomain"}})")});
	check(Api::qrLogin("fixture-key", cookies, message) == Api::QrLoginResult::Error && requests.size() == 1,
	      "QR login refuses an untrusted ticket destination before requesting it");
	reset({reply(poll), reply("", "SESSDATA=partial; DedeUserID__ckMd5=42")});
	check(Api::qrLogin("fixture-key", cookies, message) == Api::QrLoginResult::Error && cookies.empty(),
	      "partial or lookalike cookies are never accepted as a complete login");
	reset({reply(poll), {}});
	check(Api::qrLogin("fixture-key", cookies, message) == Api::QrLoginResult::Error &&
		      message.find("fixture-ticket") == std::string::npos,
	      "ticket transport failures do not expose tickets");

	Bili::Config config;
	config.room_id = "42";
	config.mid = "42";
	config.csrf_token = "fixture-csrf";
	config.cookies = complete;
	config.area_id = 86;
	const auto version = reply(R"({"code":0,"data":{"build":123,"curr_version":"fixture"}})");
	std::string address, key;
	Bili::StartLiveFollowUp followUp;
	auto start = [&]() {
		return Api::startLive(config, address, key, message, followUp);
	};
	reset({version, reply(R"({"code":60024,"msg":"当前IP开播要求粉丝数大于100"})")});
	check(!start() && Core::isIpFollowerRestriction(message) && followUp.faceUrl.empty() &&
		      followUp.faceCode == 0,
	      "msg-only follower restriction is preserved before face-code classification");
	reset({version, reply(R"({"code":60024,"data":{"qr":"https://www.bilibili.com/face"}})")});
	check(!start() && !followUp.faceUrl.empty() && followUp.faceCode == 60024,
	      "face-required responses still produce face verification");
	reset({version, reply(R"({"code":60028,"data":{"qr":"https://www.bilibili.com/new-anchor-face"}})")});
	check(!start() && !followUp.faceUrl.empty() && followUp.faceCode == 60028,
	      "new-anchor room verification follows the official 60028 flow");
	reset({version, reply(R"({"code":60043,"data":{"risk_extra":{"v_voucher":"fixture-voucher"}}})")});
	check(!start() && followUp.faceUrl.empty() && followUp.riskVoucher == "fixture-voucher" &&
		      followUp.faceCode == 60043 && !followUp.loginRequired && address.empty() && key.empty(),
	      "60043 hands the official voucher to the caller without starting or using the room QR path");
	reset({version, reply(R"({"code":60043,"data":{}})")});
	check(!start() && followUp.riskVoucher.empty() && followUp.faceUrl.empty() &&
		      followUp.faceCode == 60043 && message.find("凭证") != std::string::npos,
	      "60043 without a voucher produces an actionable error and clears the previous challenge");
	bool identified = true;
	for (const auto code : {60043, 12345}) {
		reset({});
		identified = true;
		check(!Api::isUserIdentifiedByFaceAuth(config, code, identified, message) && !identified && requests.empty(),
		      "official risk and unknown challenges cannot issue a room face-status request");
	}
	for (const auto code : {60024, 60028}) {
		reset({reply(R"({"code":0,"data":{"is_identified":true}})")});
		check(Api::isUserIdentifiedByFaceAuth(config, code, identified, message) && identified &&
			      requests.back().body.find("face_auth_code=" + std::to_string(code)) != std::string::npos,
		      "native face-status polling preserves the original challenge type");
	}
	// StartLive-compatible fixtures with a signed 32-bit secret index and UTF-8 token.
	const std::string realnameContent =
		"MTc5NTcwMDAwMDAwMHxmaXh0dXJlLXNlcnZlci1zYWx0fC0yMTQ3NDgzNTE4fGlST1dSZEg3UzBVQzAwWEJt"
		"WTZlZkpjVHpoN1Y4UUlhVG9NYWdwT0poMldIUTRjUnd2WUlFMHpFVHNlUXpZdCttVlNNRVVVbXhKbTJKZ0xk";
	const std::string otherTypeContent =
		"MTc5NTcwMDAwMDAwMHxmaXh0dXJlLXNlcnZlci1zYWx0fC0yMTQ3NDgzNTE4fGlST1dSZEg3UzBVQ3hrWEZn"
		"WVdNWmRBZHdFak85UXdSQXBzQ3hweVlpMlNBVk05ZnlmOEZFMFhQUjhYWWxKQjZsMS9QRFpPdFN3ST0=";
	const auto registration = [](const std::string &content) {
		return reply(R"({"code":0,"data":{"content":")" + content + R"("}})");
	};
	const auto unmask = [](const QByteArray &encoded) {
		const auto bytes = QByteArray::fromBase64(encoded, QByteArray::AbortOnBase64DecodingErrors);
		if (bytes.size() <= 9)
			return QByteArray{};
		QByteArray token(bytes.size() - 9, Qt::Uninitialized);
		for (qsizetype i = 0; i < token.size(); ++i)
			token[i] = static_cast<char>(bytes[i + 1] ^ bytes[0] ^ bytes[token.size() + 1 + i % 8]);
		return token;
	};
	const std::string qrPrefix = "https://www.bilibili.com/h5/risk-control/realname?t=";
	const QByteArray fixtureToken = "fixture-challenge-token-中文";
	Bili::RealnameChallenge realname;
	reset({registration(realnameContent)});
	check(Api::registerRealnameChallenge(config, "fixture-voucher", realname, message) &&
		      requests.size() == 1 && requests.back().method == "POST-no-redirect" &&
		      requests.back().url == "https://api.bilibili.com/x/gaia-vgate/v2/register" &&
		      requests.back().body == "v_voucher=fixture-voucher&dm_track=%5B%5D&csrf=fixture-csrf" &&
		      requests.back().headers.back() == "Cookie: " + complete,
	      "realname registration uses the current voucher and authenticated official endpoint");
	check(realname.qrUrl.rfind(qrPrefix, 0) == 0 &&
		      unmask(QByteArray::fromPercentEncoding(QByteArray::fromPercentEncoding(
			      QByteArray::fromStdString(realname.qrUrl.substr(qrPrefix.size()))))) == fixtureToken &&
		      unmask(QByteArray::fromStdString(realname.pollToken)) == fixtureToken,
	      "mobile QR and polling token both decode to the current UTF-8 challenge token");
	using Status = Bili::FaceAuthStatus;
	for (const auto &[response, status] : std::vector<std::pair<std::string, Status>>{
		     {R"({"code":0,"data":{"status":0}})", Status::Pending},
		     {R"({"code":0,"data":{"status":1}})", Status::Verified},
		     {R"({"code":0,"data":{"status":2}})", Status::Expired},
		     {R"({"code":0,"data":{"status":3}})", Status::Expired},
		     {R"({"code":0,"data":{"status":"1"}})", Status::Unavailable},
		     {R"({"code":0,"data":{"status":true}})", Status::Unavailable},
		     {R"({"code":0,"data":{}})", Status::Unavailable},
		     {R"({"code":-101,"data":{"status":1}})", Status::LoginRequired}}) {
		reset({reply(response)});
		check(Api::queryRealnameChallenge(config, realname.pollToken) == status && requests.size() == 1 &&
			      requests.back().method == "POST-no-redirect" &&
			      requests.back().url == "https://api.bilibili.com/x/gaia-vgate/v2/validatePreCheck" &&
			      requests.back().body == "token=" + Core::encodeFormComponent(realname.pollToken) +
								"&dm_track=%7B%22dm_img_list%22%3A%5B%5D%7D"
								"&csrf=fixture-csrf",
		      "the poll distinguishes verification, challenge expiry and account expiry");
	}
	reset({registration(otherTypeContent)});
	check(!Api::registerRealnameChallenge(config, "fixture-voucher", realname, message) &&
		      realname.qrUrl.empty() && realname.pollToken.empty() && message.find("直播中心") != std::string::npos,
	      "other challenge types send the user to Live Center without a QR or retained token");
	reset({registration(realnameContent)});
	check(!Api::registerRealnameChallenge(config, "wrong-voucher", realname, message) &&
		      realname.qrUrl.empty() && realname.pollToken.empty(),
	      "a registration response cannot open a QR for another voucher");
	const auto originalCookies = config.cookies;
	const auto started = reply(R"({"code":0,"data":{"rtmp":{"addr":"rtmp://fixture/live","code":"fixture-key"}}})");
	Http::HttpResponse timedOut;
	timedOut.timeout = true;
	const auto signInRequired = reply(R"({"code":-101,"message":"账号未登录"})");
	reset({signInRequired});
	check(!start() && followUp.loginRequired && requests.size() == 1,
	      "an explicit login failure in the version request prevents submitting start");
	reset({version, signInRequired});
	check(!start() && followUp.loginRequired && requests.size() == 2 && address.empty() && key.empty(),
	      "only an explicit start reply can mark the saved account signed out");
	reset({version, reply(R"({"code":-400,"msg":"登录已过期"})")});
	check(!start() && followUp.loginRequired, "an explicit expiry message can require login");
	reset({version, reply(R"({"code":-111,"message":"csrf 校验失败"})")});
	check(!start() && !followUp.loginRequired, "a CSRF error alone does not expire the saved account");
	bool loginRequired = false;
	reset({signInRequired});
	check(!Api::registerRealnameChallenge(config, "fixture-voucher", realname, message, &loginRequired) &&
		      loginRequired && realname.qrUrl.empty(),
	      "official verification registration reports an expired login without producing a challenge");
	reset({signInRequired});
	check(!Api::isUserIdentifiedByFaceAuth(config, 60024, identified, message, &loginRequired) && loginRequired,
	      "the room verification query reports explicit login expiry");
	reset({timedOut});
	check(!start() && followUp.versionTimedOut && !followUp.loginRequired &&
		      requests.size() == 1 &&
		      requests.front().method == "GET" && requests.front().headers.back() == "Cookie: " + originalCookies,
	      "version timeout allows retry before any start POST");
	reset({version, timedOut});
	check(!start() && !followUp.versionTimedOut && !followUp.loginRequired && requests.size() == 2,
	      "an ambiguous POST timeout is not classified as a retryable version GET timeout");
	const auto latestVersion = reply(R"({"code":0,"data":{"build":456,"curr_version":"latest-fixture"}})");
	reset({latestVersion, started});
	check(start() && config.cookies == originalCookies && followUp.faceUrl.empty() &&
		      followUp.riskVoucher.empty() && !followUp.versionTimedOut && requests.size() == 2 &&
		      requests.front().headers.back() == "Cookie: " + originalCookies &&
		      requests.back().headers.back() == "Cookie: " + originalCookies &&
		      requests.back().body.find("build=456") != std::string::npos &&
		      requests.back().body.find("version=latest-fixture") != std::string::npos,
	      "manual retry uses the newly fetched version with the original login session");
	for (const auto *bad : {R"({"code":-500})", R"({"code":-500,"message":" ","msg":"稍后重试"})",
				R"({"data":{"rtmp":{"addr":"bad","code":"bad"}}})", R"({"code":0})", "invalid"}) {
		reset({version, reply(bad)});
		address = key = "stale";
		check(!start() && !message.empty() && address.empty() && key.empty(),
		      "failed start has useful text and cannot retain old streaming credentials");
	}
	reset({reply(R"({"code":-500,"msg":"stop-denied"})")});
	check(!Api::stopLive(config, message) && message.find("stop-denied") != std::string::npos,
	      "stop errors accept msg");
	reset({reply(R"({"code":0})")});
	check(Api::updateRoomInfo(config, "A&B +中文", message) &&
		      requests.back().body.find("title=A%26B%20%2B%E4%B8%AD%E6%96%87") != std::string::npos,
	      "title updates retain encoded UTF-8 and reserved characters");
	reset({reply(R"({"msg":"missing code"})")});
	check(!Api::updateRoomArea(config, 86, message) && !message.empty(),
	      "area update without a valid status code cannot succeed");
	bool live = true;
	reset({reply(R"({"code":0,"data":{}})")});
	check(!Api::getLiveStatus(config, live, message) && !live,
	      "missing live status remains unknown instead of confirming a stopped room");
	Bili::RoomInfo roomInfo;
	reset({signInRequired});
	check(!Api::getCurrentRoomInfo(config, roomInfo, message, &loginRequired) && loginRequired,
	      "explicit room-info authentication failure is visible to the start workflow");
	reset({timedOut});
	check(!Api::getCurrentRoomInfo(config, roomInfo, message, &loginRequired) && !loginRequired,
	      "a room-info timeout does not ask the user to sign in");
	reset({reply(R"({"code":0,"data":{"by_room_ids":{"42":{"room_id":42,"uid":43,"area_id":86}}}})")});
	check(!Api::getCurrentRoomInfo(config, roomInfo, message, &loginRequired) && !loginRequired,
	      "a cached room owned by another account cannot proceed to start");
	reset({reply(
		R"({"code":0,"data":{"by_room_ids":{"42":{"room_id":42,"area_id":86,"parent_area_id":2,"live_status":1}}}})")});
	check(Api::getCurrentRoomInfo(config, roomInfo, message) && roomInfo.live_status == 1 &&
		      roomInfo.area_id == 86 && roomInfo.part_id == 2 && requests.size() == 1,
	      "one room snapshot supplies both live status and category for a matched room");
	for (const auto *body : {
		     R"({"code":0,"data":{"by_room_ids":{"42":{"room_id":42,"area_id":86}}}})",
		     R"({"code":0,"data":{"by_room_ids":{"42":{"room_id":42,"area_id":86,"live_status":3}}}})",
		     R"({"code":0,"data":{"by_room_ids":{"43":{"room_id":43,"area_id":86,"live_status":0}}}})"}) {
		reset({reply(body)});
		check(Api::getCurrentRoomInfo(config, roomInfo, message) && roomInfo.live_status < 0 &&
			      roomInfo.area_id == 86,
		      "missing, invalid or other-room status requires a separate status lookup");
	}

	Bili::Config account = config;
	account.login_status = true;
	account.title = "old account room";
	account.last_rtmp_address = "rtmp://old.example/live";
	account.last_rtmp_key = "old-fixture-key";
	account.selected_rtmp_address = "rtmp://preferred.example/live";
	account.sync_obs_start = false;
	account.proxy_unlocked = true;
	const std::string nextCookies = "SESSDATA=next-fixture; bili_jct=next-csrf; DedeUserID=43";
	reset({});
	auto restored = config;
	restored.csrf_token.clear();
	check(Core::restoreSavedAccount(restored) && restored.login_status && restored.room_id == "42" &&
		      restored.csrf_token == "fixture-csrf" && requests.empty() &&
		      Core::controlState(false, restored.login_status, !restored.room_id.empty(), true).streamToggleEnabled,
	      "saved login enables the start button without any API call");
	auto mismatched = restored;
	mismatched.cookies = nextCookies;
	check(!Core::restoreSavedAccount(mismatched) && !mismatched.login_status && !mismatched.cookies.empty(),
	      "a mismatched cached account requires a refresh without discarding the cookie");
	auto missingRoom = restored;
	missingRoom.room_id = "0";
	check(!Core::restoreSavedAccount(missingRoom) && !missingRoom.login_status && requests.empty(),
	      "a missing cached room does not trigger a startup request");
	const auto expired = Core::expiredAccount(account, {});
	check(expired.requestSucceeded && !expired.loggedIn && expired.config.cookies.empty() &&
		      expired.config.room_id.empty() && expired.config.last_rtmp_key.empty() &&
		      expired.config.selected_rtmp_address == account.selected_rtmp_address,
	      "discarding a reported expired login clears credentials and preserves the public route preference");
	const auto next = Core::prepareAccountLogin(account, nextCookies);
	check(next.room_id.empty() && next.title.empty() && next.area_id == 0 && next.part_id == 0 &&
		      next.last_rtmp_key.empty() && next.selected_rtmp_address.empty() && !next.managed_session &&
		      !next.sync_obs_start && next.sync_obs_stop && next.proxy_unlocked,
	      "changing accounts isolates room/stream data while retaining plugin preferences");
	const auto nav42 = reply(R"({"code":0,"data":{"isLogin":true,"mid":42}})");
	const auto nav43 = reply(R"({"code":0,"data":{"isLogin":true,"mid":43}})");
	const auto room42 = reply(R"({"code":0,"data":{"room_id":42}})");
	const auto offlineWithoutRoom = reply(R"({"code":0,"data":{"liveStatus":0}})");
	reset({nav43, offlineWithoutRoom, reply(R"({"code":0,"data":{"room_id":0}})")});
	auto state = Core::queryAccountState(next);
	check(state.requestSucceeded && state.loggedIn && !state.roomAvailable && state.config.room_id.empty() &&
		      state.config.last_rtmp_key.empty() && state.message.find("尚未开通直播间") != std::string::npos &&
		      requests.size() == 3,
	      "signed-in account without a room receives actionable feedback and never queries an old room");
	const auto same = Core::prepareAccountLogin(account, complete);
	check(same.selected_rtmp_address == account.selected_rtmp_address && same.last_rtmp_key.empty(),
	      "same-account login retains route preference without retaining an old publish key");
	reset({nav42, reply(R"({"code":0,"data":{"roomid":42,"liveStatus":0}})")});
	state = Core::queryAccountState(same);
	check(state.loggedIn && state.roomAvailable && state.liveStatusKnown && !state.isLive &&
		      state.config.selected_rtmp_address == account.selected_rtmp_address && state.message.empty() &&
		      requests.size() == 2 && requests.back().url.find("getRoomInfoOld") != std::string::npos,
	      "verified login loads the room ID and current status in one request and preserves route preference");
	reset({nav42, offlineWithoutRoom, timedOut, room42});
	state = Core::queryAccountState(same);
	check(state.loggedIn && state.roomAvailable && state.config.room_id == "42" && requests.size() == 4 &&
		      state.liveStatusKnown && requests[2].url == requests[3].url && requests[3].method == "GET",
	      "a single room lookup timeout retries the read-only request and recovers without a new login");
	reset({nav42, timedOut, room42});
	state = Core::queryAccountState(same);
	check(state.loggedIn && state.roomAvailable && !state.liveStatusKnown && state.config.room_id == "42" &&
		      state.message.find("查询直播状态超时") != std::string::npos && requests.size() == 3,
	      "a status timeout still discovers the room without repeating the timed-out status request");
	reset({nav42, offlineWithoutRoom, timedOut, timedOut});
	state = Core::queryAccountState(same);
	check(state.loggedIn && !state.roomAvailable && state.message.find("获取房间号超时") != std::string::npos &&
		      requests.size() == 4,
	      "repeated room lookup timeouts stop after one retry and retain login state");
	reset({nav42, offlineWithoutRoom, reply(R"({"code":-500,"message":"room lookup unavailable"})")});
	state = Core::queryAccountState(same);
	check(state.loggedIn && !state.roomAvailable && state.config.room_id.empty() &&
		      state.message.find("登录成功") != std::string::npos && requests.size() == 3,
	      "partial room lookup failure retains authentication but disables unavailable room actions");
	account.managed_session = true;
	reset({nav42, {}, {}});
	state = Core::queryAccountState(Core::prepareAccountLogin(account, complete));
	check(state.config.managed_session && state.config.room_id == account.room_id &&
		      state.config.last_rtmp_key == account.last_rtmp_key,
	      "same-account reauthentication preserves stop ownership through transient room lookup failure");
	reset({reply(R"({"code":0,"data":{"isLogin":false}})")});
	state = Core::queryAccountState(account);
	check(state.requestSucceeded && !state.loggedIn && state.config.cookies.empty() &&
		      state.config.room_id.empty() && state.config.title.empty() &&
		      state.config.last_rtmp_key.empty() &&
		      state.config.selected_rtmp_address == account.selected_rtmp_address && state.config.mid == "42" &&
		      !state.config.managed_session && !state.config.sync_obs_start && state.config.proxy_unlocked,
	      "expired authentication clears account data while retaining synchronization and proxy preferences");
	reset({reply(R"({"code":-101,"message":"账号未登录"})")});
	state = Core::queryAccountState(account);
	check(state.requestSucceeded && !state.loggedIn && state.config.last_rtmp_key.empty() &&
		      state.config.room_id.empty(),
	      "explicit unauthenticated API status expires account data instead of retaining a stale login");
	const auto afterExpiry = Core::prepareAccountLogin(state.config, complete);
	check(afterExpiry.selected_rtmp_address == account.selected_rtmp_address && afterExpiry.last_rtmp_key.empty(),
	      "same-account login after expiry retains only the public route preference");
	check(Core::prepareAccountLogin(state.config, nextCookies).selected_rtmp_address.empty(),
	      "a different account cannot inherit a remembered route after expiry");
	for (const auto &failed : {reply(R"({"code":-101})"), reply(R"({"code":0,"data":{"isLogin":false}})"), nav43}) {
		reset({failed});
		const auto candidate = Core::queryAccountState(Core::prepareAccountLogin(account, complete));
		auto current = account;
		check(Core::applyAccountRefresh(current, candidate, true) == Core::AccountApplyDecision::Failed &&
			      current.cookies == account.cookies && current.managed_session &&
			      current.room_id == account.room_id && current.last_rtmp_key == account.last_rtmp_key,
		      "failed candidate authentication cannot replace a valid managed account or stop ownership");
	}
	Core::AccountResult delayed;
	delayed.requestSucceeded = delayed.loggedIn = true;
	delayed.config = account;
	auto alreadyStopped = account;
	alreadyStopped.managed_session = false;
	check(Core::applyAccountRefresh(alreadyStopped, delayed, true) == Core::AccountApplyDecision::Applied &&
		      !alreadyStopped.managed_session,
	      "a late account snapshot cannot resurrect ownership after managed stop completed");
	delayed.config = next;
	delayed.config.login_status = true;
	auto stillStreaming = account;
	check(Core::applyAccountRefresh(stillStreaming, delayed, true) ==
			      Core::AccountApplyDecision::ManagedAccountChange &&
		      stillStreaming.cookies == account.cookies && stillStreaming.managed_session,
	      "switching accounts cannot replace the account owning an active managed broadcast");
	reset({nav43});
	state = Core::queryAccountState(account);
	check(!state.loggedIn && state.config.cookies.empty() && state.config.last_rtmp_key.empty() &&
		      requests.size() == 1,
	      "server identity and cookie identity must agree before loading a room");
	reset({{}});
	state = Core::queryAccountState(account);
	check(!state.requestSucceeded && state.config.last_rtmp_key == account.last_rtmp_key,
	      "a transient nav transport failure cannot silently expire the active account");
	reset({reply(R"({"code":0,"data":{"isLogin":true}})")});
	std::string mid;
	check(!Api::checkLoginStatus(complete, live, message, mid) && !live,
	      "missing verified account identity cannot authenticate a room lookup");
	check(replies.empty(), "all API fixtures consumed");
	std::cout << "API fixtures: " << failures << " failure(s)\n";
	return failures ? 1 : 0;
}
