#pragma once
#include <QByteArray>
#include <QHash>
#include <QString>

namespace Proxy {
class RtmpRewriter {
public:
	explicit RtmpRewriter(QByteArray upstreamUrl) : m_url(std::move(upstreamUrl))
	{
		// Match librtmp's RTMP_ParseURL: drop one trailing slash from the application.
		if (m_url.endsWith('/') && m_url.indexOf('/', m_url.indexOf("://") + 3) < m_url.size() - 1)
			m_url.chop(1);
	}
	bool feed(const QByteArray &input, QByteArray &output, QString &error);

private:
	struct Chunk {
		bool valid = false, extended = false;
		quint32 timestamp = 0, delta = 0, length = 0, streamId = 0;
		quint8 type = 0;
		QByteArray data;
	};
	QByteArray m_url, m_buffer;
	QHash<int, Chunk> m_chunks;
	quint32 m_chunkSize = 128;
	int m_handshake = 3073;
	bool m_connected = false;
};
} // namespace Proxy
