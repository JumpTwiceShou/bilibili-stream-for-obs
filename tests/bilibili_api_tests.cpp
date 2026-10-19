#include "bilibili_api.hpp"
#include "core/api_utils.hpp"
#include "core/account_state.hpp"
#include "core/login_utils.hpp"
#include "http_client.hpp"
#include <QCoreApplication>
#include <deque>
#include <iostream>

namespace {
int failures = 0;
struct Request {
	std::string method, url, body;
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
HttpResponse HttpClient::get(const std::string &url, const std::vector<std::string> &, long)
{
	return take("GET", url);
}
HttpResponse HttpClient::getLoginTicket(const std::string &url, const std::vector<std::string> &, long)
{
	return take("TICKET", url);
}
HttpResponse HttpClient::post(const std::string &url, const std::string &body, const std::vector<std::string> &, long)
{
	return take("POST", url, body);
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
	std::string address, key, face, mid = "42";
	int faceCode = 0;
	reset({version, reply(R"({"code":60024,"msg":"当前IP开播要求粉丝数大于100"})")});
	check(!Api::startLive(config, address, key, message, face, faceCode, mid) &&
		      Core::isIpFollowerRestriction(message) && face.empty() && faceCode == 0,
	      "msg-only follower restriction is preserved before face-code classification");
	reset({version, reply(R"({"code":60024,"data":{"qr":"https://www.bilibili.com/face"}})")});
	check(!Api::startLive(config, address, key, message, face, faceCode, mid) && !face.empty() && faceCode == 60024,
	      "face-required responses still produce face verification");
	for (const auto *bad : {R"({"code":-500})", R"({"code":-500,"message":" ","msg":"稍后重试"})",
				R"({"data":{"rtmp":{"addr":"bad","code":"bad"}}})", R"({"code":0})", "invalid"}) {
		reset({version, reply(bad)});
		address = key = "stale";
		check(!Api::startLive(config, address, key, message, face, faceCode, mid) && !message.empty() &&
			      address.empty() && key.empty(),
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

	Bili::Config account = config;
	account.login_status = true;
	account.title = "old account room";
	account.last_rtmp_address = "rtmp://old.example/live";
	account.last_rtmp_key = "old-fixture-key";
	account.selected_rtmp_address = "rtmp://preferred.example/live";
	account.sync_obs_start = false;
	account.proxy_unlocked = true;
	const std::string nextCookies = "SESSDATA=next-fixture; bili_jct=next-csrf; DedeUserID=43";
	const auto next = Core::prepareAccountLogin(account, nextCookies);
	check(next.room_id.empty() && next.title.empty() && next.area_id == 0 && next.part_id == 0 &&
		      next.last_rtmp_key.empty() && next.selected_rtmp_address.empty() && !next.managed_session &&
		      !next.sync_obs_start && next.sync_obs_stop && next.proxy_unlocked,
	      "changing accounts isolates room/stream data while retaining plugin preferences");
	const auto nav42 = reply(R"({"code":0,"data":{"isLogin":true,"mid":42}})");
	const auto nav43 = reply(R"({"code":0,"data":{"isLogin":true,"mid":43}})");
	const auto room42 = reply(R"({"code":0,"data":{"room_id":42}})");
	reset({nav43, reply(R"({"code":0,"data":{"room_id":0}})")});
	auto state = Core::queryAccountState(next);
	check(state.requestSucceeded && state.loggedIn && !state.roomAvailable && state.config.room_id.empty() &&
		      state.config.last_rtmp_key.empty() && state.message.find("尚未开通直播间") != std::string::npos &&
		      requests.size() == 2,
	      "signed-in account without a room receives actionable feedback and never queries an old room");
	const auto same = Core::prepareAccountLogin(account, complete);
	check(same.selected_rtmp_address == account.selected_rtmp_address && same.last_rtmp_key.empty(),
	      "same-account login retains route preference without retaining an old publish key");
	reset({nav42, room42, reply(R"({"code":0,"data":{"liveStatus":0}})")});
	state = Core::queryAccountState(same);
	check(state.loggedIn && state.roomAvailable && state.liveStatusKnown && !state.isLive &&
		      state.config.selected_rtmp_address == account.selected_rtmp_address && state.message.empty(),
	      "same-account verified login preserves route preference and enables a confirmed room");
	reset({nav42, reply(R"({"code":-500,"message":"room lookup unavailable"})")});
	state = Core::queryAccountState(same);
	check(state.loggedIn && !state.roomAvailable && state.config.room_id.empty() &&
		      state.message.find("登录成功") != std::string::npos && requests.size() == 2,
	      "partial room lookup failure retains authentication but disables unavailable room actions");
	account.managed_session = true;
	reset({nav42, {}});
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
	check(!Api::checkLoginStatus(complete, live, message, mid) && !live,
	      "missing verified account identity cannot authenticate a room lookup");
	check(replies.empty(), "all API fixtures consumed");
	std::cout << "API fixtures: " << failures << " failure(s)\n";
	return failures ? 1 : 0;
}
