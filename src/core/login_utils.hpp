#pragma once
#include <string>

namespace Core {
bool trustedLoginUrl(const std::string &url);
bool completeLoginCookies(const std::string &cookies);
std::string mergeLoginCookies(const std::string &first, const std::string &second);
std::string legacyLoginCookies(const std::string &url);
} // namespace Core
