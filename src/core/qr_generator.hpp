#pragma once
#include <QPixmap>
#include <QString>
#include <string>

namespace Core {
class QrGenerator {
public:
	static QPixmap generate(const std::string &data, int maxSize = 0);
	static QPixmap generate(const QString &data, int maxSize = 0);
};
} // namespace Core
