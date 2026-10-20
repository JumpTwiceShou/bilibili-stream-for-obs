#pragma once
#include <string>
#include <vector>
#include "json11/json11.hpp"

namespace Bili {
namespace {
const std::string APP_KEY = "aae92bc66f3edfab";
const std::string APP_SECRET = "af125a0d5279fd576c1b4418a3e8276d";
} // namespace

struct Config {
	std::string room_id;
	std::string csrf_token;
	std::string cookies;
	std::string mid;
	std::string title;
	bool login_status = false;
	bool managed_session = false;
	bool sync_obs_start = true;
	bool sync_obs_stop = true;
	bool proxy_unlocked = false;
	std::string last_rtmp_address, last_rtmp_key, selected_rtmp_address;
	int part_id = 0;
	int area_id = 0;
};

struct RoomInfo {
	std::string room_id;
	std::string title;
	std::string parent_area_name;
	std::string area_name;
	int part_id = 0;
	int area_id = 0;
	int live_status = 0;
};

class BiliApi {
public:
	enum class QrLoginResult { Pending, Scanned, Success, Expired, Error };

	static bool init();
	static void cleanup();
	static bool getQrCode(const std::string &cookies, std::string &qr_data, std::string &qr_key,
			      std::string &message);
	static QrLoginResult qrLogin(const std::string &qr_key, std::string &cookies, std::string &message);
	static bool checkLoginStatus(const std::string &cookies, bool &is_logged_in, std::string &message,
				     std::string &mid);
	static bool getRoomIdAndCsrf(const std::string &cookies, std::string &room_id, std::string &csrf_token,
				     std::string &message);
	static bool getCurrentRoomInfo(const Config &config, RoomInfo &room_info, std::string &message);
	static bool getLiveStatus(const Config &config, bool &is_live, std::string &message);
	static json11::Json getPartitionList(std::string &message);
	static bool startLive(Config &config, std::string &rtmp_addr, std::string &rtmp_code, std::string &message,
			      std::string &face_qr, int &face_auth_code, std::string &mid,
			      std::string *risk_voucher = nullptr, const std::string &gaia_token = {});
	static bool isUserIdentifiedByFaceAuth(const Config &config, int face_auth_code, bool &is_identified,
					       std::string &message);
	static bool stopLive(const Config &config, std::string &message);
	static bool updateRoomInfo(const Config &config, const std::string &title, std::string &message);
	static bool updateRoomArea(const Config &config, int area_id, std::string &message);

private:
	static std::vector<std::string> buildHeaders(const std::string &cookies);
	static std::string appsign(const std::vector<std::pair<std::string, std::string>> &params,
				   const std::string &app_key, const std::string &app_sec);
};
} // namespace Bili
