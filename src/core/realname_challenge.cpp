#include "core/realname_challenge.hpp"
#include "json11/json11.hpp"

#include <QByteArray>
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QUrl>

namespace Bili {
namespace {
// Adapted from StartLive 1.4.0's captcha_codec.py and face_captcha.py (GPL-3.0).
constexpr int maskSize = 9; // One mask byte followed by an eight-byte salt.

QByteArray maskToken(const QByteArray &token)
{
	auto *random = QRandomGenerator::system();
	QByteArray bytes(token.size() + maskSize, Qt::Uninitialized);
	bytes[0] = static_cast<char>(random->bounded(256));
	for (int i = 1; i < maskSize; ++i)
		bytes[token.size() + i] = static_cast<char>(random->bounded(256));
	for (qsizetype i = 0; i < token.size(); ++i)
		bytes[i + 1] = static_cast<char>(token[i] ^ bytes[0] ^ bytes[token.size() + 1 + i % (maskSize - 1)]);
	return bytes.toBase64();
}
} // namespace

RealnameDecodeResult decodeRealnameChallenge(const std::string &voucher, const std::string &content,
					     RealnameChallenge &challenge)
{
	challenge = {};
	if (voucher.empty() || voucher.size() > 4096 || content.empty() || content.size() > 65536)
		return RealnameDecodeResult::Invalid;
	const auto envelope =
		QByteArray::fromBase64(QByteArray::fromStdString(content), QByteArray::AbortOnBase64DecodingErrors)
			.split('|');
	if (envelope.size() != 4 || envelope[0].isEmpty() || envelope[1].isEmpty())
		return RealnameDecodeResult::Invalid;
	bool hasIndex = false;
	const auto index = envelope[2].toLongLong(&hasIndex);
	if (!hasIndex || index < -0x80000000LL || index > 0xffffffffLL)
		return RealnameDecodeResult::Invalid;
	const auto bits = static_cast<quint32>(index);
	int first = -1;
	int second = -1;
	for (int bit = 0; bit < 32 && second < 0; ++bit) {
		if (bits & (quint32{1} << bit)) {
			if (first < 0)
				first = bit;
			else
				second = bit;
		}
	}
	const auto source = QByteArray::fromStdString(voucher.substr(voucher.size() > 8 ? 8 : 0));
	if (second < 0 || first >= source.size())
		return RealnameDecodeResult::Invalid;
	const auto key =
		QCryptographicHash::hash(source.mid(first, second - first) + envelope[0], QCryptographicHash::Sha256)
			.left(16);
	auto decoded = QByteArray::fromBase64(envelope[3], QByteArray::AbortOnBase64DecodingErrors);
	for (qsizetype i = 0; i < decoded.size(); ++i)
		decoded[i] = static_cast<char>(decoded[i] ^ key[i % key.size()]);
	std::string error;
	const auto reply = json11::Json::parse(decoded.toStdString(), error);
	const auto &type = reply["type"].string_value();
	if (!error.empty() || type.empty())
		return RealnameDecodeResult::Invalid;
	if (type != "realname")
		return RealnameDecodeResult::OtherType;
	const auto &token = reply["token"].string_value();
	if (token.empty() || token.size() > 1024)
		return RealnameDecodeResult::Invalid;

	const auto rawToken = QByteArray::fromStdString(token);
	const auto qrToken = maskToken(rawToken);
	// The mobile handoff decodes the query component twice. Preserve '/' as StartLive does.
	const auto encode = [](const QByteArray &value) {
		return QUrl::toPercentEncoding(QString::fromLatin1(value), "/");
	};
	const auto url = "https://www.bilibili.com/h5/risk-control/realname?t=" + encode(encode(qrToken));
	if (url.size() > 2048)
		return RealnameDecodeResult::Invalid;
	challenge = {url.toStdString(), maskToken(rawToken).toStdString()};
	return RealnameDecodeResult::Ready;
}
} // namespace Bili
