#pragma once
#include <QByteArray>
#include <QRegularExpression>

namespace Proxy {
struct OpenVpnStatus {
	QString state;
	bool authenticationRejected = false;
	bool certificateRejected = false;
};

inline OpenVpnStatus parseOpenVpnStatus(const QByteArray &output)
{
	const QString text = QString::fromUtf8(output);
	OpenVpnStatus status;
	status.state = QRegularExpression("(?m)^State\\s*:?\\s+(connected|connecting|auth-pending|error)\\s*$")
			       .match(text)
			       .captured(1);
	if (status.state == "error") {
		const auto lower = text.toLower();
		status.authenticationRejected = lower.contains("authentication failed") ||
						lower.contains("auth_failed");
		status.certificateRejected = lower.contains("certificate verify failed") ||
					     lower.contains("certificate verification failed") ||
					     lower.contains("unknown authority");
	}
	return status;
}
} // namespace Proxy
