#pragma once

#include <string>

namespace Bili {
struct RealnameChallenge {
	std::string qrUrl;
	std::string pollToken;
};

enum class RealnameDecodeResult { Ready, OtherType, Invalid };

RealnameDecodeResult decodeRealnameChallenge(const std::string &voucher, const std::string &content,
					     RealnameChallenge &challenge);
} // namespace Bili
