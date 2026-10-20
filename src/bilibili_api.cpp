#include "bilibili_api.hpp"
#include "core/api_utils.hpp"
#include "core/login_utils.hpp"
#include "http_client.hpp"
#include "md5.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include "plugin_utils.hpp"

namespace Bili {
namespace {
static const std::vector<std::string> default_headers = {
	"Accept: application/json, text/plain, */*",
	"Accept-Language: zh-CN,zh;q=0.9,en;q=0.8,en-GB;q=0.7,en-US;q=0.6",
	"Content-Type: application/x-www-form-urlencoded; charset=UTF-8",
	"Origin: https://link.bilibili.com",
	"Referer: https://link.bilibili.com/p/center/index",
	"Sec-Ch-Ua: \"Microsoft Edge\";v=\"129\", \"Not=A?Brand\";v=\"8\", \"Chromium\";v=\"129\"",
	"Sec-Ch-Ua-Mobile: ?0",
	"Sec-Ch-Ua-Platform: \"Windows\"",
	"Sec-Fetch-Dest: empty",
	"Sec-Fetch-Mode: cors",
	"Sec-Fetch-Site: same-site",
	std::string("User-Agent: ") + Core::browserUserAgent};

bool requireSuccessfulResponse(const Http::HttpResponse &response, const char *action, std::string &message)
{
	if (response.status == 200) {
		obs_log(LOG_DEBUG, "%s completed with HTTP status 200", action);
		return true;
	}
	if (response.cancelled) {
		message = std::string(action) + "已取消";
	} else if (response.timeout) {
		message = std::string(action) + "超时";
	} else if (response.status == 0) {
		message = std::string(action) + "失败: " + response.data;
	} else {
		message = std::string(action) + "失败，HTTP 状态码: " + std::to_string(response.status);
	}
	obs_log(LOG_WARNING, "%s failed with HTTP status %ld%s", action, response.status,
		response.timeout     ? " (timeout)"
		: response.cancelled ? " (cancelled)"
				     : "");
	return false;
}

bool readApiReply(const Http::HttpResponse &response, const char *action, json11::Json &json, std::string &message,
		  int *responseCode = nullptr)
{
	std::string error;
	json = json11::Json::parse(response.data, error);
	int code = 0;
	if (!error.empty() || !Core::apiResponseCode(json, code)) {
		message = std::string(action) + "失败: 响应不是有效的 API 数据";
		return false;
	}
	if (responseCode) {
		*responseCode = code;
	} else if (code != 0) {
		message = Core::apiErrorMessage(json, action);
		obs_log(LOG_WARNING, "%s failed with API code %d", action, code);
		return false;
	}
	return true;
}
} // namespace

std::vector<std::string> BiliApi::buildHeaders(const std::string &cookies)
{
	std::vector<std::string> headers = default_headers;
	if (!cookies.empty()) {
		headers.push_back("Cookie: " + cookies);
	}
	return headers;
}

std::string BiliApi::appsign(const std::vector<std::pair<std::string, std::string>> &params, const std::string &app_key,
			     const std::string &app_sec)
{
	std::vector<std::pair<std::string, std::string>> sorted_params = params;
	sorted_params.emplace_back("appkey", app_key);
	std::sort(sorted_params.begin(), sorted_params.end());

	std::string query_str = Core::buildFormBody(sorted_params) + app_sec;

	unsigned char digest[16];
	Crypto::MD5Context ctx;
	Crypto::md5Init(&ctx);
	Crypto::md5Update(&ctx, reinterpret_cast<const unsigned char *>(query_str.c_str()), query_str.length());
	Crypto::md5Final(digest, &ctx);

	char md5_hex[33];
	for (int i = 0; i < 16; ++i) {
		snprintf(&md5_hex[i * 2], 3, "%02x", digest[i]);
	}
	md5_hex[32] = '\0';
	sorted_params.emplace_back("sign", md5_hex);

	return Core::buildFormBody(sorted_params);
}

bool BiliApi::init()
{
	return Http::HttpClient::init();
}

void BiliApi::cleanup()
{
	Http::HttpClient::cleanup();
}

bool BiliApi::getQrCode(const std::string &cookies, std::string &qr_data, std::string &qr_key, std::string &message)
{
	auto headers = buildHeaders(cookies);
	auto response =
		Http::HttpClient::get("https://passport.bilibili.com/x/passport-login/web/qrcode/generate", headers);
	if (!requireSuccessfulResponse(response, "获取二维码", message)) {
		return false;
	}

	json11::Json json;
	if (!readApiReply(response, "获取二维码", json, message)) {
		return false;
	}

	qr_data = json["data"]["url"].string_value();
	qr_key = json["data"]["qrcode_key"].string_value();
	if (qr_data.empty() || qr_key.empty()) {
		message = "无法提取二维码数据或密钥";
		return false;
	}

	message = "获取二维码成功";
	return true;
}

BiliApi::QrLoginResult BiliApi::qrLogin(const std::string &qr_key, std::string &cookies, std::string &message)
{
	cookies.clear();
	std::string url = "https://passport.bilibili.com/x/passport-login/web/qrcode/poll?qrcode_key=" +
			  Core::encodeFormComponent(qr_key);
	auto response = Http::HttpClient::get(url, default_headers);
	if (!requireSuccessfulResponse(response, "检查二维码登录状态", message)) {
		return QrLoginResult::Error;
	}

	json11::Json json;
	if (!readApiReply(response, "检查二维码登录状态", json, message)) {
		return QrLoginResult::Error;
	}

	int code = -1;
	if (!Core::jsonInteger(json["data"]["code"], code)) {
		message = "检查二维码登录状态失败: 缺少有效的扫码状态";
		return QrLoginResult::Error;
	}
	if (code != 0) {
		if (code == 86038) {
			message = "二维码已失效，请重新扫码";
			return QrLoginResult::Expired;
		} else if (code == 86090) {
			message = "二维码已扫描，等待确认";
			return QrLoginResult::Scanned;
		} else if (code == 86101) {
			message = "等待扫描二维码";
			return QrLoginResult::Pending;
		} else {
			message = Core::apiErrorMessage(json["data"], "二维码登录");
			return QrLoginResult::Error;
		}
	}

	std::string candidate = Core::mergeLoginCookies({}, response.cookies);
	const std::string loginUrl = json["data"]["url"].string_value();
	if (!Core::completeLoginCookies(candidate))
		candidate = Core::mergeLoginCookies(Core::legacyLoginCookies(loginUrl), candidate);
	if (!Core::completeLoginCookies(candidate) && !loginUrl.empty()) {
		if (!Core::trustedLoginUrl(loginUrl)) {
			message = "二维码登录失败: 返回的登录跳转地址无效";
			return QrLoginResult::Error;
		}
		const auto ticket = Http::HttpClient::getLoginTicket(loginUrl, default_headers);
		if (!requireSuccessfulResponse(ticket, "获取登录凭据", message))
			return QrLoginResult::Error;
		candidate = Core::mergeLoginCookies(candidate, ticket.cookies);
	}
	if (!Core::completeLoginCookies(candidate)) {
		message = "二维码已确认，但未获取到完整登录凭据，请重新扫码";
		return QrLoginResult::Error;
	}

	cookies = std::move(candidate);
	message = "二维码登录成功";
	return QrLoginResult::Success;
}

bool BiliApi::checkLoginStatus(const std::string &cookies, bool &is_logged_in, std::string &message, std::string &mid)
{
	is_logged_in = false;
	auto headers = buildHeaders(cookies);
	auto response = Http::HttpClient::get("https://api.bilibili.com/x/web-interface/nav", headers);
	if (!requireSuccessfulResponse(response, "检查登录状态", message)) {
		return false;
	}

	json11::Json json;
	std::string parseError;
	const auto statusReply = json11::Json::parse(response.data, parseError);
	int statusCode = 0;
	if (parseError.empty() && Core::apiResponseCode(statusReply, statusCode) && statusCode == -101) {
		mid.clear();
		message = "Bilibili 登录已失效，请重新扫码登录";
		return true;
	}
	if (!readApiReply(response, "检查登录状态", json, message)) {
		return false;
	}
	if (!json["data"]["isLogin"].is_bool()) {
		message = "检查登录状态失败: 缺少有效的登录状态";
		return false;
	}

	is_logged_in = json["data"]["isLogin"].bool_value();
	if (is_logged_in) {
		const auto &id = json["data"]["mid"];
		const double number = id.number_value();
		if (!id.is_number() || !std::isfinite(number) || std::floor(number) != number || number <= 0 ||
		    number > 9007199254740991.0) {
			is_logged_in = false;
			message = "检查登录状态失败: 缺少有效的账号标识";
			return false;
		}
		mid = std::to_string(static_cast<long long>(number));
	}
	obs_log(LOG_INFO, "检查登录状态: %s", is_logged_in ? "已登录" : "未登录");
	message = "检查登录状态: " + std::string(is_logged_in ? "已登录" : "未登录");
	return true;
}

bool BiliApi::getRoomIdAndCsrf(const std::string &cookies, std::string &room_id, std::string &csrf_token,
			       std::string &message)
{
	if (cookies.empty()) {
		obs_log(LOG_ERROR, "Cookies 为空");
		message = "Cookies 为空";
		return false;
	}

	const std::string dede_user_id = Core::extractCookieValue(cookies, "DedeUserID");
	if (dede_user_id.empty()) {
		obs_log(LOG_ERROR, "无法从 Cookies 中提取 DedeUserID");
		message = "无法从 Cookies 中提取 DedeUserID";
		return false;
	}

	std::string url = "https://api.live.bilibili.com/room/v2/Room/room_id_by_uid?uid=" +
			  Core::encodeFormComponent(dede_user_id);
	auto headers = buildHeaders(cookies);
	auto response = Http::HttpClient::get(url, headers);
	if (!requireSuccessfulResponse(response, "获取房间号", message)) {
		return false;
	}

	json11::Json json;
	if (!readApiReply(response, "获取房间号", json, message)) {
		return false;
	}

	int parsedRoomId = 0;
	if (!Core::jsonInteger(json["data"]["room_id"], parsedRoomId)) {
		message = "获取房间号失败: 返回的房间号无效";
		return false;
	}
	if (parsedRoomId == 0) {
		message = "该账号尚未开通直播间，请先在 Bilibili 直播中心开通直播间后重试。";
		return false;
	}
	room_id = std::to_string(parsedRoomId);
	csrf_token = Core::extractCookieValue(cookies, "bili_jct");
	if (csrf_token.empty()) {
		message = "无法从 Cookies 中提取登录校验信息";
		return false;
	}
	return true;
}

bool BiliApi::getCurrentRoomInfo(const Config &config, RoomInfo &room_info, std::string &message)
{
	if (config.room_id.empty()) {
		message = "无法获取当前直播间信息: room_id 为空";
		return false;
	}

	auto headers = buildHeaders(config.cookies);
	const std::string url =
		"https://api.live.bilibili.com/xlive/web-room/v1/index/getRoomBaseInfo?req_biz=web_room_componet&room_ids=" +
		Core::encodeFormComponent(config.room_id);
	auto response = Http::HttpClient::get(url, headers);
	if (!requireSuccessfulResponse(response, "获取当前直播间信息", message)) {
		return false;
	}

	json11::Json json;
	if (!readApiReply(response, "获取当前直播间信息", json, message)) {
		return false;
	}

	json11::Json room_json = json["data"]["by_room_ids"][config.room_id];
	if (room_json.is_null()) {
		const auto &room_items = json["data"]["by_room_ids"].object_items();
		if (!room_items.empty()) {
			room_json = room_items.begin()->second;
		}
	}

	if (room_json.is_null()) {
		message = "获取当前直播间信息失败: 返回数据为空";
		return false;
	}

	room_info.room_id = config.room_id;
	room_info.title = room_json["title"].string_value();
	room_info.part_id = room_json["parent_area_id"].int_value();
	room_info.area_id = room_json["area_id"].int_value();
	room_info.parent_area_name = room_json["parent_area_name"].string_value();
	room_info.area_name = room_json["area_name"].string_value();
	room_info.live_status = room_json["live_status"].int_value();

	message = "获取当前直播间信息成功";
	return true;
}

bool BiliApi::getLiveStatus(const Config &config, bool &is_live, std::string &message)
{
	is_live = false;
	std::string mid = config.mid;
	if (mid.empty()) {
		mid = Core::extractCookieValue(config.cookies, "DedeUserID");
	}

	if (mid.empty()) {
		message = "无法获取用户 mid";
		return false;
	}

	auto headers = buildHeaders(config.cookies);
	auto response = Http::HttpClient::get("https://api.live.bilibili.com/room/v1/Room/getRoomInfoOld?mid=" +
						      Core::encodeFormComponent(mid),
					      headers);
	if (!requireSuccessfulResponse(response, "查询直播状态", message)) {
		return false;
	}

	json11::Json json;
	if (!readApiReply(response, "查询直播状态", json, message)) {
		return false;
	}

	int status = -1;
	if (!Core::jsonInteger(json["data"]["liveStatus"], status) || status > 2) {
		message = "查询直播状态失败: 返回的直播状态无效";
		return false;
	}

	is_live = status == 1;
	message = is_live ? "当前直播状态: 直播中" : "当前直播状态: 未开播";
	obs_log(LOG_INFO, "%s", message.c_str());
	return true;
}

json11::Json BiliApi::getPartitionList(std::string &message)
{
	auto response = Http::HttpClient::get("https://api.live.bilibili.com/room/v1/Area/getList", default_headers);
	if (!requireSuccessfulResponse(response, "获取分区列表", message)) {
		return json11::Json();
	}

	json11::Json json;
	if (!readApiReply(response, "获取分区列表", json, message))
		return json11::Json();
	if (!json["data"].is_array()) {
		message = "获取分区列表失败: 响应中缺少分区数组";
		return json11::Json();
	}
	message = "获取分区列表成功";
	return json["data"];
}

bool BiliApi::startLive(Config &config, std::string &rtmp_addr, std::string &rtmp_code, std::string &message,
			std::string &face_qr, int &face_auth_code, std::string &mid, std::string *risk_voucher,
			const std::string &gaia_token)
{
	(void)mid;
	if (risk_voucher)
		risk_voucher->clear();
	face_qr.clear();
	face_auth_code = 0;
	rtmp_addr.clear();
	rtmp_code.clear();
	if (config.room_id.empty() || config.csrf_token.empty()) {
		obs_log(LOG_ERROR, "Start-live configuration is incomplete");
		message = "开播配置无效，请重新登录并更新直播间信息";
		return false;
	}

	std::vector<std::pair<std::string, std::string>> version_params = {{"system_version", "2"},
									   {"ts", std::to_string(time(nullptr))}};
	std::string version_query = appsign(version_params, APP_KEY, APP_SECRET);
	std::string version_url =
		"https://api.live.bilibili.com/xlive/app-blink/v1/liveVersionInfo/getHomePageLiveVersion?" +
		version_query;

	// The official SDK's short-lived token applies only to this attempt, never saved login state.
	auto headers = buildHeaders(
		config.cookies +
		(gaia_token.empty() ? "" : "; x-bili-gaia-vtoken=" + Core::encodeFormComponent(gaia_token)));
	auto version_response = Http::HttpClient::get(version_url, headers);
	if (!requireSuccessfulResponse(version_response, "获取直播版本信息", message)) {
		return false;
	}

	json11::Json json;
	if (!readApiReply(version_response, "获取直播版本信息", json, message)) {
		return false;
	}

	int build = 0;
	std::string curr_version = json["data"]["curr_version"].string_value();
	if (!Core::jsonInteger(json["data"]["build"], build, 1) || curr_version.empty()) {
		obs_log(LOG_ERROR, "无效的 build 或 curr_version");
		message = "无效的 build 或 curr_version";
		return false;
	}

	std::vector<std::pair<std::string, std::string>> start_params = {{"room_id", config.room_id},
									 {"platform", "pc_link"},
									 {"area_v2", std::to_string(config.area_id)},
									 {"backup_stream", "0"},
									 {"csrf_token", config.csrf_token},
									 {"csrf", config.csrf_token},
									 {"build", std::to_string(build)},
									 {"version", curr_version},
									 {"ts", std::to_string(time(nullptr))}};
	std::string start_data = appsign(start_params, APP_KEY, APP_SECRET);

	auto response =
		Http::HttpClient::post("https://api.live.bilibili.com/room/v1/Room/startLive", start_data, headers);
	if (!requireSuccessfulResponse(response, "启动直播", message)) {
		return false;
	}

	int code = 0;
	if (!readApiReply(response, "启动直播", json, message, &code)) {
		return false;
	}
	if (code != 0) {
		message = Core::apiErrorMessage(json, "启动直播");
		// Preserve the actionable restriction message before classifying numeric face-auth codes.
		if (Core::isIpFollowerRestriction(message))
			return false;

		if (code == 60024 || code == 60028) {
			std::string face_url = json["data"]["qr"].string_value();
			obs_log(LOG_WARNING, "Start-live requires room face authentication (code %d)", code);
			message = "需要人脸验证，请扫描二维码";
			face_qr = face_url;
			face_auth_code = code;
			return false;
		}
		if (code == 60043) {
			// Current official live center delegates 60043 to CaptchaLoader with this voucher.
			// A generic face-auth-middle URL cannot complete this particular risk challenge.
			const auto voucher = json["data"]["risk_extra"]["v_voucher"].string_value();
			obs_log(LOG_WARNING, "Start-live requires official risk verification (code 60043)");
			if (risk_voucher && !voucher.empty()) {
				*risk_voucher = voucher;
				face_auth_code = code;
				message = "需要完成本次官方安全验证";
			} else {
				message = "B 站要求官方安全验证，但缺少本次验证凭证，请重新尝试开播";
			}
			return false;
		}
		return false;
	}

	rtmp_addr = json["data"]["rtmp"]["addr"].string_value();
	rtmp_code = json["data"]["rtmp"]["code"].string_value();
	if (rtmp_addr.empty() || rtmp_code.empty()) {
		message = "开播成功，但响应中缺少 RTMP 地址或推流码";
		return false;
	}
	return true;
}

bool BiliApi::isUserIdentifiedByFaceAuth(const Config &config, int face_auth_code, bool &is_identified,
					 std::string &message)
{
	is_identified = false;
	if (config.room_id.empty() || config.csrf_token.empty() ||
	    (face_auth_code != 60024 && face_auth_code != 60028)) {
		message = "查询人脸认证状态失败: 配置或认证类型无效";
		return false;
	}

	std::string data = Core::buildFormBody({{"room_id", config.room_id},
						{"face_auth_code", std::to_string(face_auth_code)},
						{"csrf_token", config.csrf_token},
						{"csrf", config.csrf_token}});
	auto headers = buildHeaders(config.cookies);
	auto response = Http::HttpClient::post(
		"https://api.live.bilibili.com/xlive/app-blink/v1/preLive/IsUserIdentifiedByFaceAuth", data, headers);
	if (!requireSuccessfulResponse(response, "查询人脸认证状态", message)) {
		return false;
	}

	json11::Json json;
	if (!readApiReply(response, "查询人脸认证状态", json, message)) {
		return false;
	}

	if (!Core::faceAuthIdentified(json, is_identified)) {
		message = "查询人脸认证状态失败: 无效的认证状态响应";
		return false;
	}
	message = is_identified ? "人脸认证已完成，正在自动开播..." : "等待人脸认证完成...";
	return true;
}

bool BiliApi::stopLive(const Config &config, std::string &message)
{
	std::string data = Core::buildFormBody({{"room_id", config.room_id},
						{"platform", "pc_link"},
						{"csrf_token", config.csrf_token},
						{"csrf", config.csrf_token}});
	auto headers = buildHeaders(config.cookies);
	auto response = Http::HttpClient::post("https://api.live.bilibili.com/room/v1/Room/stopLive", data, headers);
	if (!requireSuccessfulResponse(response, "停止直播", message)) {
		return false;
	}

	json11::Json json;
	if (!readApiReply(response, "停止直播", json, message)) {
		return false;
	}

	obs_log(LOG_INFO, "直播已停止");
	return true;
}

bool BiliApi::updateRoomInfo(const Config &config, const std::string &title, std::string &message)
{
	std::string data = Core::buildFormBody({{"room_id", config.room_id},
						{"platform", "pc_link"},
						{"title", title},
						{"csrf_token", config.csrf_token},
						{"csrf", config.csrf_token}});
	auto headers = buildHeaders(config.cookies);
	auto response = Http::HttpClient::post("https://api.live.bilibili.com/room/v1/Room/update", data, headers);
	if (!requireSuccessfulResponse(response, "更新直播间信息", message)) {
		return false;
	}

	json11::Json json;
	if (!readApiReply(response, "更新直播间信息", json, message)) {
		return false;
	}

	obs_log(LOG_INFO, "直播间信息更新成功: %s", title.c_str());
	return true;
}

bool BiliApi::updateRoomArea(const Config &config, int area_id, std::string &message)
{
	if (config.room_id.empty() || config.csrf_token.empty()) {
		message = "更新直播间分区失败: room_id 或 csrf_token 为空";
		return false;
	}

	std::string data = Core::buildFormBody({{"platform", "pc"},
						{"room_id", config.room_id},
						{"area_id", std::to_string(area_id)},
						{"csrf_token", config.csrf_token},
						{"csrf", config.csrf_token}});
	auto headers = buildHeaders(config.cookies);
	auto response = Http::HttpClient::post(
		"https://api.live.bilibili.com/xlive/app-blink/v2/room/AnchorChangeRoomArea", data, headers);
	if (!requireSuccessfulResponse(response, "更新直播间分区", message)) {
		return false;
	}

	json11::Json json;
	if (!readApiReply(response, "更新直播间分区", json, message)) {
		return false;
	}

	obs_log(LOG_INFO, "直播间分区更新成功: %d", area_id);
	return true;
}
} // namespace Bili
