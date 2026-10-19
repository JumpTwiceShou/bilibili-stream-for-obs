#pragma once
#include <QKeyEvent>
#include <QMouseEvent>
#include <QObject>
#include <QPushButton>
#include <functional>

namespace UI {
// Hold the native start gesture until fresh credentials and face authentication are ready.
class NativeStartFilter : public QObject {
public:
	NativeStartFilter(QPushButton *button, std::function<bool()> intercept, std::function<void()> start,
			  QObject *parent)
		: QObject(parent),
		  m_button(button),
		  m_intercept(std::move(intercept)),
		  m_start(std::move(start))
	{
		button->installEventFilter(this);
	}

protected:
	bool eventFilter(QObject *, QEvent *event) override
	{
		if (!m_intercept())
			return false;
		if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick) {
			auto *mouse = static_cast<QMouseEvent *>(event);
			if (mouse->button() != Qt::LeftButton)
				return false;
			m_pressed = true;
			m_button->setDown(true);
			return true;
		}
		if (event->type() == QEvent::MouseButtonRelease) {
			auto *mouse = static_cast<QMouseEvent *>(event);
			if (mouse->button() != Qt::LeftButton)
				return false;
			const bool activate = m_pressed && m_button->rect().contains(mouse->position().toPoint());
			m_pressed = false;
			m_button->setDown(false);
			if (activate)
				m_start();
			return true;
		}
		if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
			auto *key = static_cast<QKeyEvent *>(event);
			if (key->key() != Qt::Key_Space && key->key() != Qt::Key_Return && key->key() != Qt::Key_Enter)
				return false;
			if (!key->isAutoRepeat() && event->type() == QEvent::KeyRelease)
				m_start();
			return true;
		}
		return false;
	}

private:
	QPushButton *m_button;
	bool m_pressed = false;
	std::function<bool()> m_intercept;
	std::function<void()> m_start;
};
} // namespace UI
