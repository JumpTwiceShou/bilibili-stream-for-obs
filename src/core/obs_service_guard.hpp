#pragma once
#include <obs.h>
#include <memory>
#include <atomic>
#include <string>

namespace Core {
void registerManagedService();
class ObsServiceGuard {
public:
	~ObsServiceGuard();
	bool install(const std::string &url, const std::string &key);
	void restore();

private:
	obs_service_t *m_previous = nullptr, *m_service = nullptr;
	std::shared_ptr<std::atomic_bool> m_ready;
};
} // namespace Core
