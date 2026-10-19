#pragma once

#include <QCheckBox>
#include <QMouseEvent>
#include <QPainter>
#include <QVariantAnimation>

namespace UI {
// QCheckBox preserves keyboard navigation, accessibility and the standard toggled signal.
class ToggleSwitch : public QCheckBox {
public:
	explicit ToggleSwitch(const QString &label, QWidget *parent = nullptr) : QCheckBox(label, parent)
	{
		setCursor(Qt::PointingHandCursor);
		setMinimumHeight(32);
		m_animation.setDuration(140);
		connect(&m_animation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
			m_position = value.toReal();
			update();
		});
		connect(&m_animation, &QVariantAnimation::finished, this, [this] { update(); });
		connect(this, &QCheckBox::toggled, this, [this](bool checked) {
			m_animation.stop();
			m_animation.setStartValue(m_position);
			m_animation.setEndValue(checked ? 1.0 : 0.0);
			m_animation.start();
		});
	}
	QSize sizeHint() const override { return {fontMetrics().horizontalAdvance(text()) + 76, 34}; }

protected:
	bool hitButton(const QPoint &point) const override { return rect().contains(point); }
	void mousePressEvent(QMouseEvent *event) override
	{
		m_press = event->position().toPoint();
		QCheckBox::mousePressEvent(event);
	}
	void mouseReleaseEvent(QMouseEvent *event) override
	{
		const int distance = event->position().toPoint().x() - m_press.x();
		if (event->button() == Qt::LeftButton && isDown() && qAbs(distance) > 12) {
			setDown(false);
			setChecked(distance > 0);
			event->accept();
			return;
		}
		QCheckBox::mouseReleaseEvent(event);
	}
	void paintEvent(QPaintEvent *) override
	{
		QPainter painter(this);
		painter.setRenderHint(QPainter::Antialiasing);
		painter.setOpacity(isEnabled() ? 1.0 : 0.45);
		const QRectF track(width() - 52, (height() - 26) / 2.0, 48, 26);
		painter.setPen(Qt::NoPen);
		painter.setBrush(isChecked() ? QColor("#34c759") : QColor("#777980"));
		painter.drawRoundedRect(track, 13, 13);
		painter.setBrush(Qt::white);
		const qreal position = m_animation.state() == QAbstractAnimation::Running ? m_position
											  : (isChecked() ? 1 : 0);
		painter.drawEllipse(QRectF(track.x() + 3 + 22 * position, track.y() + 3, 20, 20));
		painter.setPen(palette().color(QPalette::WindowText));
		painter.drawText(rect().adjusted(0, 0, -64, 0), Qt::AlignVCenter | Qt::AlignLeft, text());
		if (hasFocus()) {
			painter.setPen(QPen(palette().color(QPalette::Highlight), 1, Qt::DotLine));
			painter.setBrush(Qt::NoBrush);
			painter.drawRoundedRect(track.adjusted(-2, -2, 2, 2), 15, 15);
		}
	}

private:
	QVariantAnimation m_animation;
	qreal m_position = 0;
	QPoint m_press;
};
} // namespace UI
