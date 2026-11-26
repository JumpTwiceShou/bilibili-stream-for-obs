#include "core/qr_generator.hpp"
#include "qrcodegen/qrcodegen.hpp"
#include <QImage>
#include <QPainter>
#include <algorithm>

namespace Core {
QPixmap QrGenerator::generate(const std::string &data, int maxSize)
{
	return generate(QString::fromStdString(data), maxSize);
}

QPixmap QrGenerator::generate(const QString &data, int maxSize)
{
	if (data.isEmpty()) {
		return QPixmap();
	}

	using qrcodegen::QrCode;
	const QrCode qr = QrCode::encodeText(data.toUtf8().constData(), QrCode::Ecc::LOW);
	if (qr.getSize() <= 0) {
		return QPixmap();
	}

	const int border = 4;
	const int size = qr.getSize();
	const int scale = maxSize > 0 ? std::clamp(maxSize / (size + border * 2), 1, 10) : 10;
	QImage image((size + border * 2) * scale, (size + border * 2) * scale, QImage::Format_RGB32);
	image.fill(Qt::white);
	QPainter painter(&image);
	painter.setPen(Qt::NoPen);
	painter.setBrush(Qt::black);

	for (int y = 0; y < size; ++y) {
		for (int x = 0; x < size; ++x) {
			if (qr.getModule(x, y)) {
				painter.drawRect((x + border) * scale, (y + border) * scale, scale, scale);
			}
		}
	}
	return QPixmap::fromImage(image);
}
} // namespace Core
