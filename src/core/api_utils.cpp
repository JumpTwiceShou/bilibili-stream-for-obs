#include "core/api_utils.hpp"

#include <charconv>
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

bool shouldStopManagedSession(bool managedSession, bool statusKnown, bool isLive)
{
	return managedSession && (!statusKnown || isLive);
}

ControlState controlState(bool busy, bool loggedIn, bool hasRoomId, bool hasCredentials)
{
	return {!busy, !busy && hasCredentials, !busy && loggedIn, !busy && loggedIn, !busy && hasRoomId};
}
} // namespace Core
