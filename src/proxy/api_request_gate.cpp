#include "api_request_gate.hpp"
#include <QCoreApplication>
#include <QPointer>

namespace Proxy {
ApiRequestGate::ApiRequestGate(Runtime &runtime, std::function<Settings()> settings, std::function<bool(Task)> dispatch,
			       QObject *parent)
	: QObject(parent),
	  m_runtime(runtime),
	  m_settings(std::move(settings)),
	  m_dispatch(std::move(dispatch))
{
	m_wait.setInterval(100);
	connect(&m_wait, &QTimer::timeout, this, [this] { poll(); });
}

bool ApiRequestGate::enqueue(Task task)
{
	++m_count;
	if (!m_settings().apiEnabled() || m_runtime.endpoint().ready())
		return dispatch(std::move(task));
	m_pending.push_back(std::move(task));
	if (!m_wait.isActive()) {
		m_elapsed.start();
		m_wait.start();
		m_runtime.setAutoReconnect(true);
		if (!m_runtime.starting())
			m_runtime.start(m_settings());
	}
	return true;
}

bool ApiRequestGate::dispatch(Task task)
{
	const QPointer<ApiRequestGate> guard(this);
	const bool accepted = m_dispatch([guard, task = std::move(task)] {
		task();
		if (auto *app = QCoreApplication::instance())
			QMetaObject::invokeMethod(
				app,
				[guard] {
					if (guard) {
						--guard->m_count;
						guard->releaseIfIdle();
					}
				},
				Qt::QueuedConnection);
	});
	if (!accepted) {
		--m_count;
		releaseIfIdle();
	}
	return accepted;
}

void ApiRequestGate::poll()
{
	if (!m_runtime.endpoint().ready() && m_runtime.starting() && m_elapsed.elapsed() < 90000)
		return;
	if (!m_runtime.endpoint().ready() && m_elapsed.elapsed() >= 90000) {
		m_runtime.setAutoReconnect(false);
		m_runtime.stop();
	}
	m_wait.stop();
	auto pending = std::move(m_pending);
	m_pending.clear();
	// A failed connection still dispatches the request against the fail-closed HTTP endpoint,
	// allowing the existing request completion/error callbacks to resolve their UI state.
	for (auto &task : pending)
		dispatch(std::move(task));
}

void ApiRequestGate::setHeld(bool held)
{
	m_held = held;
	releaseIfIdle();
}

void ApiRequestGate::releaseIfIdle()
{
	if (!busy() && m_settings().onDemandApi() &&
	    !(m_runtime.hasFailure() && !m_runtime.starting() && !m_runtime.endpoint().ready())) {
		m_runtime.setAutoReconnect(false);
		m_runtime.stop();
	}
}
} // namespace Proxy
