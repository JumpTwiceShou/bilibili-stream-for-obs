#include "bilibili_api.hpp"
#include "http_client.hpp"
#include "md5.hpp"
#include <algorithm>
#include <sstream>
#include <iostream>
#include "plugin_utils.hpp"
#include "util/base.h"

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
	"User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/129.0.0.0 Safari/537.36"};

std::string extractCookieValue(const std::string &cookies, const std::string &key)
{
	const std::string needle = key + "=";
	const size_t start = cookies.find(needle);
	if (start == std::string::npos)
		return "";

	const size_t value_start = start + needle.length();
	const size_t value_end = cookies.find(';', value_start);
	if (value_end == std::string::npos)
		return cookies.substr(value_start);

	return cookies.substr(value_start, value_end - value_start);
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

	std::ostringstream query;
	for (size_t i = 0; i < sorted_params.size(); ++i) {
		if (i > 0)
			query << "&";
		query << sorted_params[i].first << "=" << sorted_params[i].second;
	}
	std::string query_str = query.str() + app_sec;

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

	query.str("");
	for (size_t i = 0; i < sorted_params.size(); ++i) {
		if (i > 0)
			query << "&";
		query << sorted_params[i].first << "=" << sorted_params[i].second;
	}
	return query.str();
}

void BiliApi::init()
{
	Http::HttpClient::init();
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
	if (response.status != 200) {
		obs_log(LOG_ERROR, "获取二维码失败，状态码: %d, 数据: %s", response.status, response.data.c_str());
		message = "获取二维码失败，状态码: " + std::to_string(response.status);
		if (!response.data.empty()) {
			message += ", 数据: " + response.data;
		}
		return false;
	}

	std::string err;
	json11::Json json = json11::Json::parse(response.data, err);
	if (!err.empty()) {
		message = "Json 解析失败: " + err;
		return false;
	}

	qr_data = json["data"]["url"].string_value();
	qr_key = json["data"]["qrcode_key"].string_value();
	if (qr_data.empty() || qr_key.empty()) {
		message = "无法提取二维码数据或密钥";
		return false;
	}

	message = "获取二维码成功，URL: " + qr_data + ", Key: " + qr_key;
	return true;
}

bool BiliApi::qrLogin(std::string &qr_key, std::string &cookies, std::string &message)
{
	std::string url = "https://passport.bilibili.com/x/passport-login/web/qrcode/poll?qrcode_key=" + qr_key;
	auto response = Http::HttpClient::get(url, default_headers);
	obs_log(LOG_INFO, "检查二维码登录状态: %s", response.data.c_str());
	if (response.status != 200) {
		message = "检查二维码登录状态失败，状态码: " + std::to_string(response.status);
		if (!response.data.empty()) {

			message += ", 数据: " + response.data;
		}
		return false;
	}

	std::string err;
	json11::Json json = json11::Json::parse(response.data, err);
	if (!err.empty()) {
		obs_log(LOG_ERROR, "JSON 解析失败: %s", err.c_str());
		message = "JSON 解析失败: " + err;
		return false;
	}

	int code = json["data"]["code"].int_value();
	if (code != 0) {
		if (code == 86038) {
			obs_log(LOG_ERROR, "二维码已失效: %s", json["message"].string_value().c_str());
			message = "二维码已失效: " + json["message"].string_value();
		} else if (code == 86090) {
			obs_log(LOG_INFO, "二维码已扫描，等待确认");
			message = "二维码已扫描，等待确认";
		} else {
			obs_log(LOG_ERROR, "API 返回错误，code: %d, message: %s", code,
				json["message"].string_value().c_str());
			message = "API 返回错误，code: " + std::to_string(code) +
				  ", message: " + json["message"].string_value();
		}
		return false;
	}

	cookies = response.cookies;
	if (cookies.empty()) {
		std::string loginUrl = json["data"]["url"].string_value();
		if (!loginUrl.empty()) {
			obs_log(LOG_INFO, "从 URL 解析 Cookies...");

			auto extractParam = [&](const std::string &key) -> std::string {
				std::string search = key + "=";
				size_t start = loginUrl.find(search);
				if (start == std::string::npos)
					return "";
				start += search.length();
				size_t end = loginUrl.find("&", start);
				if (end == std::string::npos)
					end = loginUrl.length();
				return loginUrl.substr(start, end - start);
			};

			std::string sessData = extractParam("SESSDATA");
			std::string biliJct = extractParam("bili_jct"); // 即 csrf
			std::string dedeUserId = extractParam("DedeUserID");

			if (!sessData.empty() && !biliJct.empty()) {
				cookies = "SESSDATA=" + sessData + "; bili_jct=" + biliJct +
					  "; DedeUserID=" + dedeUserId + ";";
			}
		}
	}
	if (cookies.empty()) {
		obs_log(LOG_ERROR, "无法获取登录 Cookies");
		message = "无法获取登录 Cookies";
		return false;
	}

	message = "二维码登录成功";
	return true;
}

bool BiliApi::checkLoginStatus(const std::string &cookies, std::string &message, std::string &mid)
{
	auto headers = buildHeaders(cookies);
	auto response = Http::HttpClient::get("https://api.bilibili.com/x/web-interface/nav", headers);
	obs_log(LOG_INFO, "检查登录状态: %s", response.data.c_str());
	if (response.status != 200) {
		message = "检查登录状态失败，状态码: " + std::to_string(response.status);
		if (!response.data.empty()) {
			message += ", 数据: " + response.data;
		}
		return false;
	}

	std::string err;
	json11::Json json = json11::Json::parse(response.data, err);
	if (!err.empty()) {
		obs_log(LOG_ERROR, "JSON 解析失败: %s", err.c_str());
		message = "JSON 解析失败: " + err;
		return false;
	}

	bool is_login = json["data"]["isLogin"].bool_value();
	if (is_login) {
		mid = std::to_string(static_cast<long long>(json["data"]["mid"].number_value()));
	}
	obs_log(LOG_INFO, "检查登录状态: %s", is_login ? "已登录" : "未登录");
	message = "检查登录状态: " + std::string(is_login ? "已登录" : "未登录");
	return is_login;
}

bool BiliApi::getRoomIdAndCsrf(const std::string &cookies, std::string &room_id, std::string &csrf_token,
			       std::string &message)
{
	if (cookies.empty()) {
		obs_log(LOG_ERROR, "Cookies 为空");
		message = "Cookies 为空";
		return false;
	}

	const std::string dede_user_id = extractCookieValue(cookies, "DedeUserID");
	if (dede_user_id.empty()) {
		obs_log(LOG_ERROR, "无法从 Cookies 中提取 DedeUserID");
		message = "无法从 Cookies 中提取 DedeUserID";
		return false;
	}

	std::string url = "https://api.live.bilibili.com/room/v2/Room/room_id_by_uid?uid=" + dede_user_id;
	auto headers = buildHeaders(cookies);
	auto response = Http::HttpClient::get(url, headers);
	obs_log(LOG_INFO, "获取房间号: %s", response.data.c_str());
	if (response.status != 200) {
		message = "获取房间号失败，状态码: " + std::to_string(response.status);
		if (!response.data.empty()) {
			message += ", 数据: " + response.data;
		}
		return false;
	}

	std::string err;
	json11::Json json = json11::Json::parse(response.data, err);
	if (!err.empty()) {
		obs_log(LOG_ERROR, "JSON 解析失败: %s", err.c_str());
		message = "Json 解析失败: " + err;
		return false;
	}
	if (json["code"].int_value() != 0) {
		obs_log(LOG_ERROR, "API 返回错误，code: %d, message: %s", json["code"].int_value(),
			json["message"].string_value().c_str());
		message = "API 返回错误， code: " + std::to_string(json["code"].int_value()) +
			  ", message: " + json["message"].string_value();
		return false;
	}

	room_id = std::to_string(json["data"]["room_id"].int_value());
	csrf_token = extractCookieValue(cookies, "bili_jct");
	if (csrf_token.empty()) {
		//obs_log(LOG_ERROR, "无法从 Cookies 中提取 bili_jct");
		return false;
	}

	//obs_log(LOG_INFO, "获取 room_id 和 csrf_token 成功: room_id=%s, csrf_token=%s", room_id.c_str(), csrf_token.c_str());
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
		config.room_id;
	auto response = Http::HttpClient::get(url, headers);
	obs_log(LOG_INFO, "获取当前直播间信息: %s", response.data.c_str());
	if (response.status != 200) {
		message = "获取当前直播间信息失败，状态码: " + std::to_string(response.status);
		if (!response.data.empty()) {
			message += ", 数据: " + response.data;
		}
		return false;
	}

	std::string err;
	json11::Json json = json11::Json::parse(response.data, err);
	if (!err.empty()) {
		obs_log(LOG_ERROR, "获取当前直播间信息 JSON 解析失败: %s", err.c_str());
		message = "获取当前直播间信息 JSON 解析失败: " + err;
		return false;
	}

	if (json["code"].int_value() != 0) {
		message = "获取当前直播间信息失败: " + json["message"].string_value();
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
	std::string mid = config.mid;
	if (mid.empty()) {
		mid = extractCookieValue(config.cookies, "DedeUserID");
	}

	if (mid.empty()) {
		message = "无法获取用户 mid";
		return false;
	}

	auto headers = buildHeaders(config.cookies);
	auto response =
		Http::HttpClient::get("https://api.live.bilibili.com/room/v1/Room/getRoomInfoOld?mid=" + mid, headers);
	obs_log(LOG_INFO, "查询直播状态: %s", response.data.c_str());
	if (response.status != 200) {
		message = "查询直播状态失败，状态码: " + std::to_string(response.status);
		if (!response.data.empty()) {
			message += ", 数据: " + response.data;
		}
		return false;
	}

	std::string err;
	json11::Json json = json11::Json::parse(response.data, err);
	if (!err.empty()) {
		obs_log(LOG_ERROR, "查询直播状态 JSON 解析失败: %s", err.c_str());
		message = "查询直播状态 JSON 解析失败: " + err;
		return false;
	}

	if (json["code"].int_value() != 0) {
		message = "查询直播状态失败: " + json["message"].string_value();
		return false;
	}

	if (json["data"].is_null()) {
		message = "查询直播状态失败: 返回数据为空";
		return false;
	}

	is_live = json["data"]["liveStatus"].int_value() == 1;
	message = is_live ? "当前直播状态: 直播中" : "当前直播状态: 未开播";
	obs_log(LOG_INFO, "%s", message.c_str());
	return true;
}

json11::Json BiliApi::getPartitionList(std::string &message)
{
	auto response = Http::HttpClient::get("https://api.live.bilibili.com/room/v1/Area/getList", default_headers);
	obs_log(LOG_INFO, "获取分区列表: %s", response.data.c_str());
	if (response.status != 200) {
		obs_log(LOG_ERROR, "获取分区列表失败，状态码: %d", response.status);
		message = "获取分区列表失败，状态码: " + std::to_string(response.status);
		if (!response.data.empty()) {
			message += ", 数据: " + response.data;
		}
		return json11::Json();
	}

	std::string err;
	json11::Json json = json11::Json::parse(response.data, err);
	if (!err.empty() || !json["data"].is_array()) {
		message = "解析分区列表失败: " + std::string(err.empty() ? "无数据数组" : err);
		return json11::Json();
	}
	message = "获取分区列表成功";
	return json["data"];
}

bool BiliApi::startLive(Config &config, std::string &rtmp_addr, std::string &rtmp_code, std::string &message,
			std::string &face_qr, std::string &mid)
{
	if (config.room_id.empty() || config.csrf_token.empty()) {
		obs_log(LOG_ERROR, "配置无效: room_id=%s, csrf_token=%s, title=%s",
			config.room_id.c_str(), config.csrf_token.c_str(), config.title.c_str());
		message = "配置无效: 房间号=" + config.room_id + ", csrf_token=" + config.csrf_token;
		return false;
	}

	std::vector<std::pair<std::string, std::string>> version_params = {{"system_version", "2"},
									   {"ts", std::to_string(time(nullptr))}};
	std::string version_query = appsign(version_params, APP_KEY, APP_SECRET);
	std::string version_url =
		"https://api.live.bilibili.com/xlive/app-blink/v1/liveVersionInfo/getHomePageLiveVersion?" +
		version_query;

	auto headers = buildHeaders(config.cookies);
	auto version_response = Http::HttpClient::get(version_url, headers);
	obs_log(LOG_INFO, "获取直播版本信息: %s", version_response.data.c_str());
	if (version_response.status != 200) {
		obs_log(LOG_ERROR, "获取直播版本信息失败，状态码: %ld", version_response.status);
		message = "获取直播版本信息失败，状态码: " + std::to_string(version_response.status);
		return false;
	}

	std::string err;
	json11::Json json = json11::Json::parse(version_response.data, err);
	if (!err.empty() || json["code"].int_value() != 0) {
		obs_log(LOG_ERROR, "获取直播版本信息失败: %s", err.c_str());
		message = "解析直播版本信息失败: " + (err.empty() ? json["message"].string_value() : err);
		return false;
	}

	long build = json["data"]["build"].int_value();
	std::string curr_version = json["data"]["curr_version"].string_value();
	if (build == 0 || curr_version.empty()) {
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
	obs_log(LOG_INFO, "启动直播: %s", response.data.c_str());
	if (response.status != 200) {
		obs_log(LOG_ERROR, "启动直播失败，状态码: %ld", response.status);
		message = "启动直播失败，状态码: " + std::to_string(response.status);
		return false;
	}

	json = json11::Json::parse(response.data, err);
	int code = json["code"].int_value();
	obs_log(LOG_INFO, "开始直播，mid: %s", mid.c_str());
	if (code != 0) {
		message = json["message"].string_value();

		// 如果是人脸识别
		if (code == 60024) {
			std::string face_url = json["data"]["qr"].string_value();
			obs_log(LOG_WARNING, "60024 需要人脸识别，URL: %s", face_url.c_str());
			// 这里可以弹出一个对话框或者在 UI 上显示二维码
			message = "需要人脸验证，请扫描二维码" + face_url;
			face_qr = face_url;
			return false;
		}
		if (code == 60043) {
			std::string face_url = "https://www.bilibili.com/blackboard/live/face-auth-middle.html?source_event=400&mid=" + mid;
			obs_log(LOG_WARNING, "60043 需要人脸识别，URL: %s", face_url.c_str());
			message = "需要人脸验证，请扫描二维码" + face_url;
			face_qr = face_url;
			return false;
		}
		return false;
	}

	rtmp_addr = json["data"]["rtmp"]["addr"].string_value();
	rtmp_code = json["data"]["rtmp"]["code"].string_value();
	if (rtmp_addr.empty() || rtmp_code.empty()) {
		//obs_log(LOG_ERROR, "无法解析 RTMP 地址或推流码");
		return false;
	}

	//obs_log(LOG_INFO, "直播启动成功，RTMP 地址: %s, 推流码: %s", rtmp_addr.c_str(), rtmp_code.c_str());
	return true;
}

bool BiliApi::isUserIdentifiedByFaceAuth(const Config &config, bool &is_identified, std::string &message)
{
	if (config.room_id.empty() || config.csrf_token.empty()) {
		message = "查询人脸认证状态失败: room_id 或 csrf_token 为空";
		return false;
	}

	std::string data = "room_id=" + config.room_id + "&face_auth_code=60024&csrf_token=" + config.csrf_token +
			   "&csrf=" + config.csrf_token;
	auto headers = buildHeaders(config.cookies);
	auto response = Http::HttpClient::post(
		"https://api.live.bilibili.com/xlive/app-blink/v1/preLive/IsUserIdentifiedByFaceAuth", data, headers);
	obs_log(LOG_INFO, "查询人脸认证状态: %s", response.data.c_str());
	if (response.status != 200) {
		message = "查询人脸认证状态失败，状态码: " + std::to_string(response.status);
		if (!response.data.empty()) {
			message += ", 数据: " + response.data;
		}
		return false;
	}

	std::string err;
	json11::Json json = json11::Json::parse(response.data, err);
	if (!err.empty()) {
		message = "查询人脸认证状态 JSON 解析失败: " + err;
		return false;
	}

	if (json["code"].int_value() != 0) {
		message = "查询人脸认证状态失败: " + json["message"].string_value();
		return false;
	}

	is_identified = json["data"]["is_identified"].bool_value();
	message = is_identified ? "人脸认证已完成，正在自动开播..." : "等待人脸认证完成...";
	return true;
}

bool BiliApi::stopLive(const Config &config, std::string &message)
{
	std::string data = "room_id=" + config.room_id + "&platform=pc_link&csrf_token=" + config.csrf_token +
			   "&csrf=" + config.csrf_token;
	auto headers = buildHeaders(config.cookies);
	auto response = Http::HttpClient::post("https://api.live.bilibili.com/room/v1/Room/stopLive", data, headers);
	obs_log(LOG_INFO, "停止直播: %s", response.data.c_str());
	if (response.status != 200) {
		message = "停止直播失败，状态码: " + std::to_string(response.status);
		if (!response.data.empty()) {
			message += ", 数据: " + response.data;
		}
		return false;
	}

	std::string err;
	json11::Json json = json11::Json::parse(response.data, err);
	if (!err.empty() || json["code"].int_value() != 0) {
		obs_log(LOG_ERROR, "停止直播失败: %s",
			err.empty() ? json["message"].string_value().c_str() : err.c_str());
		message = "停止直播失败: " + (err.empty() ? json["message"].string_value() : err);
		return false;
	}

	obs_log(LOG_INFO, "直播已停止");
	return true;
}

bool BiliApi::updateRoomInfo(const Config &config, const std::string &title, std::string &message)
{
	std::string data = "room_id=" + config.room_id + "&platform=pc_link&title=" + title +
			   "&csrf_token=" + config.csrf_token + "&csrf=" + config.csrf_token;
	auto headers = buildHeaders(config.cookies);
	auto response = Http::HttpClient::post("https://api.live.bilibili.com/room/v1/Room/update", data, headers);
	obs_log(LOG_INFO, "更新房间信息: %s", response.data.c_str());
	if (response.status != 200) {
		obs_log(LOG_ERROR, "获取更新房间信息失败，状态码: %ld", response.status);
		message = "获取更新房间信息失败，状态码: " + std::to_string(response.status);
		if (!response.data.empty()) {
			message += ", 数据: " + response.data;
		}
		return false;
	}

	std::string err;
	json11::Json json = json11::Json::parse(response.data, err);
	if (!err.empty() || json["code"].int_value() != 0) {
		//obs_log(LOG_ERROR, "更新直播间信息失败: %s", err.empty() ? json["message"].string_value().c_str() : err.c_str());
		message = "更新直播间信息失败: " + (err.empty() ? json["message"].string_value() : err);
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

	std::string data = "platform=pc&room_id=" + config.room_id + "&area_id=" + std::to_string(area_id) +
			   "&csrf_token=" + config.csrf_token + "&csrf=" + config.csrf_token;
	auto headers = buildHeaders(config.cookies);
	auto response = Http::HttpClient::post(
		"https://api.live.bilibili.com/xlive/app-blink/v2/room/AnchorChangeRoomArea", data, headers);
	obs_log(LOG_INFO, "更新直播间分区: %s", response.data.c_str());
	if (response.status != 200) {
		message = "更新直播间分区失败，状态码: " + std::to_string(response.status);
		if (!response.data.empty()) {
			message += ", 数据: " + response.data;
		}
		return false;
	}

	std::string err;
	json11::Json json = json11::Json::parse(response.data, err);
	if (!err.empty() || json["code"].int_value() != 0) {
		message = "更新直播间分区失败: " + (err.empty() ? json["message"].string_value() : err);
		return false;
	}

	obs_log(LOG_INFO, "直播间分区更新成功: %d", area_id);
	return true;
}
} // namespace Bili
