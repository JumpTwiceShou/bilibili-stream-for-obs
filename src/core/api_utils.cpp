#include "core/api_utils.hpp"

#include <charconv>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <string_view>

namespace Core {
namespace {
std::string_view trim(std::string_view value)
{
	const auto first = value.find_first_not_of(" \t\r\n");
	if (first == std::string_view::npos) {
		return {};
	}
	const auto last = value.find_last_not_of(" \t\r\n");
	return value.substr(first, last - first + 1);
}

bool isUnreserved(unsigned char value)
{
	return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') ||
	       value == '-' || value == '.' || value == '_' || value == '~';
}
} // namespace

std::string extractCookieValue(const std::string &cookies, const std::string &key)
{
	std::string_view remaining(cookies);
	while (!remaining.empty()) {
		const auto separator = remaining.find(';');
		const auto item = trim(remaining.substr(0, separator));
		const auto equals = item.find('=');
		if (equals != std::string_view::npos && trim(item.substr(0, equals)) == key) {
			return std::string(trim(item.substr(equals + 1)));
		}
		if (separator == std::string_view::npos) {
			break;
		}
		remaining.remove_prefix(separator + 1);
	}
	return {};
}

std::string encodeFormComponent(const std::string &value)
{
	static constexpr char hex[] = "0123456789ABCDEF";
	std::string encoded;
	encoded.reserve(value.size());
	for (const unsigned char byte : value) {
		if (isUnreserved(byte)) {
			encoded.push_back(static_cast<char>(byte));
		} else {
			encoded.push_back('%');
			encoded.push_back(hex[byte >> 4]);
			encoded.push_back(hex[byte & 0x0F]);
		}
	}
	return encoded;
}

std::string buildFormBody(const std::vector<std::pair<std::string, std::string>> &params)
{
	std::string body;
	for (const auto &[key, value] : params) {
		if (!body.empty()) {
			body.push_back('&');
		}
		body += encodeFormComponent(key);
		body.push_back('=');
		body += encodeFormComponent(value);
	}
	return body;
}

bool jsonInteger(const json11::Json &value, int &result, int minimum)
{
	long long parsed = 0;
	if (value.is_number()) {
		const double number = value.number_value();
		if (!std::isfinite(number) || std::floor(number) != number ||
		    number < static_cast<double>(std::numeric_limits<int>::min()) ||
		    number > static_cast<double>(std::numeric_limits<int>::max())) {
			return false;
		}
		parsed = static_cast<long long>(number);
	} else if (value.is_string()) {
		const auto &text = value.string_value();
		if (text.empty()) {
			return false;
		}
		const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
		if (error != std::errc() || end != text.data() + text.size()) {
			return false;
		}
	} else {
		return false;
	}

	if (parsed < minimum || parsed > std::numeric_limits<int>::max()) {
		return false;
	}
	result = static_cast<int>(parsed);
	return true;
}

FaceAuthRetryDecision classifyFaceAuthRetry(bool started, bool faceAuthRequired)
{
	if (started)
		return FaceAuthRetryDecision::Started;
	return faceAuthRequired ? FaceAuthRetryDecision::ContinueWaiting : FaceAuthRetryDecision::Failed;
}

bool apiResponseCode(const json11::Json &response, int &code)
{
	return response.is_object() && jsonInteger(response["code"], code, std::numeric_limits<int>::min());
}

std::string apiErrorMessage(const json11::Json &response, const std::string &action)
{
	std::string detail;
	for (const auto *field : {"message", "msg"}) {
		const auto value = trim(response[field].string_value());
		if (!value.empty()) {
			detail = value;
			break;
		}
	}
	if (detail.size() > 512) {
		size_t end = 512;
		while (end > 0 && (static_cast<unsigned char>(detail[end]) & 0xc0) == 0x80)
			--end;
		detail.resize(end);
		detail += "…";
	}
	if (detail.empty()) {
		int code = 0;
		detail = apiResponseCode(response, code) && code != 0 ? "错误码 " + std::to_string(code)
								      : "响应字段无效";
	}
	return action + "失败: " + detail;
}

bool isIpFollowerRestriction(const std::string &message)
{
	std::string lower = message;
	std::transform(lower.begin(), lower.end(), lower.begin(),
		       [](unsigned char c) { return char(std::tolower(c)); });
	bool ip = false;
	for (size_t pos = lower.find("ip"); pos != std::string::npos; pos = lower.find("ip", pos + 2)) {
		auto letter = [](char c) {
			return c >= 'a' && c <= 'z';
		};
		if ((pos == 0 || !letter(lower[pos - 1])) && (pos + 2 == lower.size() || !letter(lower[pos + 2])))
			ip = true;
	}
	return ip && (lower.find("粉丝") != std::string::npos || lower.find("follower") != std::string::npos);
}

bool faceAuthIdentified(const json11::Json &response, bool &identified)
{
	identified = false;
	int code = -1;
	if (!jsonInteger(response["code"], code) || code != 0)
		return false;
	const auto &value = response["data"]["is_identified"];
	if (value.is_bool()) {
		identified = value.bool_value();
		return true;
	}
	int flag = -1;
	if (!jsonInteger(value, flag) || flag > 1)
		return false;
	identified = flag == 1;
	return true;
}

bool shouldStopManagedSession(bool managedSession, bool statusKnown, bool isLive)
{
	return managedSession && (!statusKnown || isLive);
}

ControlState controlState(bool busy, bool loggedIn, bool hasRoomId, bool hasCredentials)
{
	const bool roomReady = !busy && loggedIn && hasRoomId;
	return {!busy, !busy && hasCredentials, roomReady, roomReady, roomReady};
}
} // namespace Core
