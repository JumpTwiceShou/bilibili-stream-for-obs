#pragma once
#include "proxy_runtime.hpp"
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <deque>
#include <functional>

namespace Proxy {
class ApiRequestGate : public QObject {
public:
	using Task = std::function<void()>;
	ApiRequestGate(Runtime &runtime, std::function<Settings()> settings, std::function<bool(Task)> dispatch,
		       QObject *parent = nullptr);
	bool enqueue(Task task);
	void setHeld(bool held);
	bool busy() const { return m_count > 0 || m_held; }

private:
	bool dispatch(Task task);
	void poll();
	void releaseIfIdle();
	Runtime &m_runtime;
	std::function<Settings()> m_settings;
	std::function<bool(Task)> m_dispatch;
	std::deque<Task> m_pending;
	QTimer m_wait;
	QElapsedTimer m_elapsed;
	int m_count = 0;
	bool m_held = false;
};
} // namespace Proxy
