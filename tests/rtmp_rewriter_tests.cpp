#include "proxy/rtmp_rewriter.hpp"
#include <iostream>

static QByteArray stringValue(const QByteArray &value)
{
	QByteArray out(1, char(2));
	out += char(value.size() >> 8);
	out += char(value.size());
	return out + value;
}
static QByteArray property(const QByteArray &name, const QByteArray &value)
{
	QByteArray out;
	out += char(name.size() >> 8);
	out += char(name.size());
	return out + name + stringValue(value);
}
static QByteArray packet(const QByteArray &payload, int chunkSize = 128, unsigned timestamp = 0)
{
	QByteArray out = QByteArray::fromHex("030000000000001400000000");
	out[1] = char(timestamp >> 16);
	out[2] = char(timestamp >> 8);
	out[3] = char(timestamp);
	out[4] = char(payload.size() >> 16);
	out[5] = char(payload.size() >> 8);
	out[6] = char(payload.size());
	for (int pos = 0; pos < payload.size(); pos += chunkSize) {
		if (pos)
			out += char(0xc3);
		out += payload.mid(pos, chunkSize);
	}
	return out;
}
int main()
{
	int failures = 0;
	auto check = [&](bool value, const char *message) {
		if (!value) {
			++failures;
			std::cerr << "FAIL: " << message << '\n';
		}
	};
	const QByteArray upstream = "rtmp://upstream.example:1935/live";
	QByteArray connect = stringValue("connect") + QByteArray::fromHex("003ff000000000000003");
	connect += property("app", "live") + property("tcUrl", "rtmp://127.0.0.1:45678/live") +
		   property("swfUrl", "rtmp://127.0.0.1:45678/live");
	connect += property("flashVer", QByteArray(180, 'x')) + QByteArray::fromHex("000009");
	QByteArray expected = stringValue("connect") + QByteArray::fromHex("003ff000000000000003");
	expected += property("app", "live") + property("tcUrl", upstream) + property("swfUrl", upstream);
	expected += property("flashVer", QByteArray(180, 'x')) + QByteArray::fromHex("000009");
	Proxy::RtmpRewriter rewriter(upstream);
	QByteArray handshake(3073, '\0');
	handshake[0] = 3;
	const QByteArray input = handshake + packet(connect);
	QByteArray total, part;
	QString error;
	for (int i = 0; i < input.size(); ++i) {
		check(rewriter.feed(input.mid(i, 1), part, error), "fragmented RTMP remains valid");
		total += part;
	}
	check(total == handshake + packet(expected), "restore tcUrl and swfUrl, update message length and rechunk");
	Proxy::RtmpRewriter trailing(upstream + "/");
	check(trailing.feed(input, part, error) && part == handshake + packet(expected),
	      "match OBS tcUrl semantics for trailing application slash");
	const QByteArray following = stringValue("releaseStream") + QByteArray::fromHex("00400000000000000005") +
				     stringValue("private-stream-key");
	check(rewriter.feed(packet(following), part, error) && part == packet(following),
	      "preserve following command and stream key");
	Proxy::RtmpRewriter malformed(upstream);
	malformed.feed(handshake, part, error);
	check(!malformed.feed(QByteArray::fromHex("c3"), part, error),
	      "reject compressed header without preceding header");
	std::cout << "RTMP tests: " << failures << " failure(s)\n";
	return failures ? 1 : 0;
}
