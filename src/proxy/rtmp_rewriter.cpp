#include "rtmp_rewriter.hpp"
#include <algorithm>
namespace Proxy {
namespace {
quint32 be(const QByteArray &b, qsizetype p, int count)
{
	quint32 result = 0;
	for (int i = 0; i < count; ++i)
		result = (result << 8) | quint8(b[p + i]);
	return result;
}
void put(QByteArray &b, quint32 value, int count)
{
	for (int i = count - 1; i >= 0; --i)
		b += char(value >> (i * 8));
}
void basic(QByteArray &b, int id, int format)
{
	if (id < 64)
		b += char((format << 6) | id);
	else if (id < 320) {
		b += char(format << 6);
		b += char(id - 64);
	} else {
		b += char((format << 6) | 1);
		b += char(id - 64);
		b += char((id - 64) >> 8);
	}
}
// Skip an AMF0 value with bounded nesting. Connect properties only need AMF0.
bool skipValue(const QByteArray &b, qsizetype &p, int depth)
{
	if (p >= b.size() || depth > 16)
		return false;
	const auto type = quint8(b[p++]);
	if (type == 0)
		p += 8;
	else if (type == 1)
		++p;
	else if (type == 2 || type == 12) {
		const int size = type == 2 ? 2 : 4;
		if (p + size > b.size())
			return false;
		const auto length = be(b, p, size);
		p += size;
		p += length;
	} else if (type == 3 || type == 8) {
		if (type == 8)
			p += 4;
		while (p + 3 <= b.size()) {
			const auto length = be(b, p, 2);
			p += 2;
			if (!length && quint8(b[p]) == 9) {
				++p;
				return true;
			}
			p += length;
			if (!skipValue(b, p, depth + 1))
				return false;
		}
		return false;
	} else if (type == 10) {
		if (p + 4 > b.size())
			return false;
		const auto count = be(b, p, 4);
		p += 4;
		if (count > 4096)
			return false;
		for (quint32 i = 0; i < count; ++i)
			if (!skipValue(b, p, depth + 1))
				return false;
	} else if (type != 5 && type != 6)
		return false;
	return p <= b.size();
}
bool rewriteConnect(QByteArray &b, const QByteArray &url, bool &connected)
{
	if (connected || !b.startsWith(QByteArray::fromHex("020007") + "connect"))
		return true;
	qsizetype p = 10;
	if (!skipValue(b, p, 0) || p >= b.size() || quint8(b[p++]) != 3)
		return false;
	bool found = false;
	while (p + 3 <= b.size()) {
		const auto length = be(b, p, 2);
		p += 2;
		if (!length && quint8(b[p]) == 9) {
			connected = found;
			return found;
		}
		if (p + length > b.size())
			return false;
		const auto key = b.mid(p, length);
		p += length;
		const qsizetype valueStart = p;
		if (!skipValue(b, p, 0))
			return false;
		if (key == "tcUrl" || key == "swfUrl") {
			if (quint8(b[valueStart]) != 2 || url.size() > 65535)
				return false;
			QByteArray value(1, char(2));
			put(value, quint32(url.size()), 2);
			value += url;
			b.replace(valueStart, p - valueStart, value);
			p = valueStart + value.size();
			if (key == "tcUrl")
				found = true;
		}
	}
	return false;
}
} // namespace

bool RtmpRewriter::feed(const QByteArray &input, QByteArray &output, QString &error)
{
	constexpr quint32 maxMessage = 8 * 1024 * 1024;
	output.clear();
	error = QStringLiteral("RTMP 数据格式无效或缓冲超过限制。");
	m_buffer += input;
	if (m_buffer.size() > maxMessage)
		return false;
	if (m_handshake) {
		const int count = int((std::min)(qsizetype(m_handshake), m_buffer.size()));
		if (m_handshake == 3073 && count && quint8(m_buffer[0]) != 3)
			return false;
		output += m_buffer.left(count);
		m_buffer.remove(0, count);
		m_handshake -= count;
		if (m_handshake) {
			error.clear();
			return true;
		}
	}
	while (!m_buffer.isEmpty()) {
		const quint8 first = quint8(m_buffer[0]);
		const int format = first >> 6;
		int id = first & 63, pos = 1;
		if (id < 2) {
			const int bytes = id == 0 ? 1 : 2;
			if (m_buffer.size() < pos + bytes)
				break;
			id = 64 + quint8(m_buffer[pos]) + (bytes == 2 ? (quint8(m_buffer[pos + 1]) << 8) : 0);
			pos += bytes;
		}
		if (!m_chunks.contains(id) && m_chunks.size() >= 64)
			return false;
		Chunk next = m_chunks.value(id);
		if (format && !next.valid)
			return false;
		const bool continuation = !next.data.isEmpty() && quint32(next.data.size()) < next.length;
		if (continuation && format != 3)
			return false;
		const int header = format == 0 ? 11 : format == 1 ? 7 : format == 2 ? 3 : 0;
		if (m_buffer.size() < pos + header)
			break;
		quint32 stamp = 0;
		if (format != 3) {
			stamp = be(m_buffer, pos, 3);
			next.extended = stamp == 0xffffff;
			if (format <= 1) {
				next.length = be(m_buffer, pos + 3, 3);
				next.type = quint8(m_buffer[pos + 6]);
			}
			if (!format) {
				next.streamId = quint8(m_buffer[pos + 7]) | (quint32(quint8(m_buffer[pos + 8])) << 8) |
						(quint32(quint8(m_buffer[pos + 9])) << 16) |
						(quint32(quint8(m_buffer[pos + 10])) << 24);
			}
		}
		pos += header;
		if (next.extended) {
			if (m_buffer.size() < pos + 4)
				break;
			stamp = be(m_buffer, pos, 4);
			pos += 4;
		}
		if (next.length > maxMessage)
			return false;
		if (!format) {
			next.timestamp = stamp;
			next.delta = stamp;
		} else if (format < 3) {
			next.delta = stamp;
			next.timestamp += stamp;
		} else if (!continuation)
			next.timestamp += next.delta;
		const quint32 amount = (std::min)(m_chunkSize, next.length - quint32(next.data.size()));
		if (m_buffer.size() < pos + amount)
			break;
		next.valid = true;
		// Release the shared reference before append to avoid copying the whole partial message per chunk.
		m_chunks[id].data.clear();
		next.data += m_buffer.mid(pos, amount);
		m_buffer.remove(0, pos + amount);
		if (quint32(next.data.size()) == next.length) {
			if (next.type == 20 && !rewriteConnect(next.data, m_url, m_connected))
				return false;
			if (next.type == 17)
				return false; // AMF3 isn't emitted by OBS's RTMP output.
			basic(output, id, 0);
			put(output, (std::min)(next.timestamp, 0xffffffu), 3);
			put(output, quint32(next.data.size()), 3);
			output += char(next.type);
			for (int i = 0; i < 4; ++i)
				output += char(next.streamId >> (i * 8));
			if (next.timestamp >= 0xffffff)
				put(output, next.timestamp, 4);
			for (qsizetype i = 0; i < next.data.size(); i += m_chunkSize) {
				if (i) {
					basic(output, id, 3);
					if (next.timestamp >= 0xffffff)
						put(output, next.timestamp, 4);
				}
				output += next.data.mid(i, m_chunkSize);
			}
			if (next.type == 1) {
				if (next.data.size() != 4)
					return false;
				const auto size = be(next.data, 0, 4);
				if (!size || size > 1024 * 1024)
					return false;
				m_chunkSize = size;
			}
			if (next.type == 2 && next.data.size() == 4)
				m_chunks.remove(int(be(next.data, 0, 4)));
			next.data.clear();
		}
		m_chunks[id] = std::move(next);
		qsizetype buffered = 0;
		for (const auto &chunk : m_chunks)
			buffered += chunk.data.size();
		if (buffered > maxMessage)
			return false;
	}
	error.clear();
	return true;
}
} // namespace Proxy
