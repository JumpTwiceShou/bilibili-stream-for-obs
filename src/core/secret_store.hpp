#pragma once

#include <string>

namespace Core {
bool protectSecret(const std::string &plainText, std::string &protectedText, std::string &message);
bool unprotectSecret(const std::string &protectedText, std::string &plainText, std::string &message);
bool usesNativeSecretProtection();
} // namespace Core
