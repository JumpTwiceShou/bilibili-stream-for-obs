#include "login_utils.hpp"
#include "api_utils.hpp"
#include <QUrl>
#include <QUrlQuery>
#include <QStringList>
#include <array>
#include <algorithm>
#include <string_view>

namespace Core {
namespace {
constexpr std::array<const char *, 3> cookieNames{"SESSDATA", "bili_jct", "DedeUserID"};
bool safeCookie(const std::string &value)
{
	return !value.empty() && value.size() <= 8192 && value.find(';') == std::string::npos &&
	       std::none_of(value.begin(), value.end(), [](unsigned char c) { return c <= 32 || c == 127; });
}
} // namespace

bool trustedLoginUrl(const std::string &url)
{
	if (url.size() > 16384 ||
	    std::any_of(url.begin(), url.end(), [](unsigned char c) { return c <= 32 || c == 127 || c == '\\'; }))
		return false;
	const QUrl parsed(QString::fromStdString(url), QUrl::StrictMode);
	const QStringList hosts{"passport.bilibili.com", "passport.biligame.com", "www.bilibili.com", "bilibili.com"};
	return parsed.isValid() && parsed.scheme() == "https" && hosts.contains(parsed.host()) &&
	       parsed.userInfo().isEmpty() && !parsed.hasFragment() && parsed.port(443) == 443;
}

bool completeLoginCookies(const std::string &cookies)
{
	for (const auto *name : cookieNames)
		if (!safeCookie(extractCookieValue(cookies, name)))
			return false;
	return true;
}

std::string mergeLoginCookies(const std::string &first, const std::string &second)
{
	std::array<std::string, 3> values;
	for (const auto *source : {&first, &second}) {
		std::string_view remaining(*source);
		while (!remaining.empty()) {
			const auto separator = remaining.find(';');
			const std::string item(remaining.substr(0, separator));
			for (size_t i = 0; i < cookieNames.size(); ++i) {
				const auto value = extractCookieValue(item, cookieNames[i]);
				if (safeCookie(value))
					values[i] = value;
			}
			if (separator == std::string_view::npos)
				break;
			remaining.remove_prefix(separator + 1);
		}
	}
	std::string result;
	for (size_t i = 0; i < cookieNames.size(); ++i) {
		if (!values[i].empty()) {
			if (!result.empty())
				result += "; ";
			result += std::string(cookieNames[i]) + "=" + values[i];
		}
	}
	return result;
}

std::string legacyLoginCookies(const std::string &url)
{
	if (!trustedLoginUrl(url))
		return {};
	const QUrlQuery query(QUrl(QString::fromStdString(url)));
	std::string result;
	for (const auto *name : cookieNames) {
		const auto value = query.queryItemValue(QString::fromLatin1(name), QUrl::FullyEncoded).toStdString();
		if (safeCookie(value))
			result += std::string(name) + "=" + value + "; ";
	}
	return mergeLoginCookies({}, result);
}
} // namespace Core
