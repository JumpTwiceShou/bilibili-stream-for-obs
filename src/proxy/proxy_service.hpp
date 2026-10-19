#pragma once
#include "stream_relay.hpp"
#include <obs.h>
#include <functional>

namespace Proxy {
void registerService();
class ServiceGuard {
public:
	~ServiceGuard();
	bool install(const QString &url, const std::string &key, const Endpoint &endpoint, QString &error);
	void restore();
	void invalidate();
	bool ownsCurrent() const;

private:
	obs_service_t *m_previous = nullptr, *m_service = nullptr;
	std::shared_ptr<std::atomic_bool> m_ready;
	std::function<void(const QString &, const std::string &, const Endpoint &)> m_update;
};
} // namespace Proxy
