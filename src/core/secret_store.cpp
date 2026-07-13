#include "core/secret_store.hpp"

#include <cctype>
#include <limits>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dpapi.h>
#endif

namespace Core {
namespace {
constexpr const char *DPAPI_PREFIX = "dpapi-v1:";
constexpr const char *PLAIN_PREFIX = "plain-v1:";

std::string hexEncode(const unsigned char *data, size_t size)
{
	static constexpr char hex[] = "0123456789abcdef";
	std::string output;
	output.resize(size * 2);
	for (size_t index = 0; index < size; ++index) {
		output[index * 2] = hex[data[index] >> 4];
		output[index * 2 + 1] = hex[data[index] & 0x0F];
	}
	return output;
}

int hexValue(char value)
{
	if (value >= '0' && value <= '9')
		return value - '0';
	value = static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
	if (value >= 'a' && value <= 'f')
		return value - 'a' + 10;
	return -1;
}

bool hexDecode(const std::string &text, std::vector<unsigned char> &output)
{
	if (text.size() % 2 != 0)
		return false;
	output.resize(text.size() / 2);
	for (size_t index = 0; index < output.size(); ++index) {
		const int high = hexValue(text[index * 2]);
		const int low = hexValue(text[index * 2 + 1]);
		if (high < 0 || low < 0) {
			output.clear();
			return false;
		}
		output[index] = static_cast<unsigned char>((high << 4) | low);
	}
	return true;
}
} // namespace

bool protectSecret(const std::string &plainText, std::string &protectedText, std::string &message)
{
	protectedText.clear();
	message.clear();
	if (plainText.empty())
		return true;

#ifdef _WIN32
	if (plainText.size() > std::numeric_limits<DWORD>::max()) {
		message = "登录凭据过大，无法安全保存";
		return false;
	}

	DATA_BLOB input{};
	input.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(plainText.data()));
	input.cbData = static_cast<DWORD>(plainText.size());
	DATA_BLOB output{};
	if (!CryptProtectData(&input, L"OBS Bilibili login cookies", nullptr, nullptr, nullptr,
			      CRYPTPROTECT_UI_FORBIDDEN, &output)) {
		message = "Windows 无法保护登录凭据";
		return false;
	}
	protectedText = DPAPI_PREFIX + hexEncode(output.pbData, output.cbData);
	LocalFree(output.pbData);
#else
	protectedText =
		PLAIN_PREFIX + hexEncode(reinterpret_cast<const unsigned char *>(plainText.data()), plainText.size());
#endif
	return true;
}

bool unprotectSecret(const std::string &protectedText, std::string &plainText, std::string &message)
{
	plainText.clear();
	message.clear();
	if (protectedText.empty())
		return true;

	const auto decodePlainFallback = [&]() {
		std::vector<unsigned char> decoded;
		if (!hexDecode(protectedText.substr(std::char_traits<char>::length(PLAIN_PREFIX)), decoded)) {
			message = "保存的登录凭据格式无效";
			return false;
		}
		if (!decoded.empty())
			plainText.assign(reinterpret_cast<const char *>(decoded.data()), decoded.size());
		return true;
	};

	if (protectedText.rfind(PLAIN_PREFIX, 0) == 0)
		return decodePlainFallback();
	if (protectedText.rfind(DPAPI_PREFIX, 0) != 0) {
		message = "保存的登录凭据版本不受支持";
		return false;
	}

#ifdef _WIN32
	std::vector<unsigned char> encrypted;
	if (!hexDecode(protectedText.substr(std::char_traits<char>::length(DPAPI_PREFIX)), encrypted) ||
	    encrypted.size() > std::numeric_limits<DWORD>::max()) {
		message = "保存的登录凭据格式无效";
		return false;
	}
	DATA_BLOB input{};
	input.pbData = encrypted.data();
	input.cbData = static_cast<DWORD>(encrypted.size());
	DATA_BLOB output{};
	if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) {
		message = "Windows 无法解密登录凭据，可能来自其他用户或电脑";
		return false;
	}
	plainText.assign(reinterpret_cast<const char *>(output.pbData), output.cbData);
	SecureZeroMemory(output.pbData, output.cbData);
	LocalFree(output.pbData);
	return true;
#else
	message = "此平台无法解密 Windows DPAPI 登录凭据";
	return false;
#endif
}

bool usesNativeSecretProtection()
{
#ifdef _WIN32
	return true;
#else
	return false;
#endif
}
} // namespace Core
