#include "obs_service_guard.hpp"
#include <obs-frontend-api.h>

namespace Core {
namespace {
struct State {
	std::string url, key;
	std::shared_ptr<std::atomic_bool> ready = std::make_shared<std::atomic_bool>(true);
};
using Handle = std::shared_ptr<State>;
Handle pending;
State &state(void *data)
{
	return **static_cast<Handle *>(data);
}
} // namespace

void registerManagedService()
{
	obs_service_info info{};
	info.id = "bilibili_managed_stream";
	info.get_name = [](void *) {
		return "Bilibili Managed Stream";
	};
	info.create = [](obs_data_t *, obs_service_t *) -> void * {
		return new Handle(pending ? pending : std::make_shared<State>());
	};
	info.destroy = [](void *data) {
		delete static_cast<Handle *>(data);
	};
	info.get_protocol = [](void *) {
		return "RTMP";
	};
	info.get_output_type = [](void *) {
		return "rtmp_output";
	};
	info.get_url = [](void *data) {
		return state(data).url.c_str();
	};
	info.get_key = [](void *data) {
		return state(data).key.c_str();
	};
	info.get_connect_info = [](void *data, uint32_t type) -> const char * {
		if (type == OBS_SERVICE_CONNECT_INFO_SERVER_URL)
			return state(data).url.c_str();
		if (type == OBS_SERVICE_CONNECT_INFO_STREAM_KEY)
			return state(data).key.c_str();
		return nullptr;
	};
	info.can_try_to_connect = [](void *data) {
		return state(data).ready->load() && !state(data).url.empty();
	};
	obs_register_service(&info);
}

ObsServiceGuard::~ObsServiceGuard()
{
	restore();
}

bool ObsServiceGuard::install(const std::string &url, const std::string &key)
{
	if (obs_frontend_streaming_active() || url.empty() || key.empty())
		return false;
	restore();
	m_previous = obs_service_get_ref(obs_frontend_get_streaming_service());
	pending = std::make_shared<State>();
	pending->url = url;
	pending->key = key;
	m_ready = pending->ready;
	// Credentials remain in instance data; OBS cannot serialize them to a profile.
	m_service = obs_service_create("bilibili_managed_stream", "Bilibili managed stream", nullptr, nullptr);
	pending.reset();
	if (!m_service) {
		restore();
		return false;
	}
	obs_frontend_set_streaming_service(m_service);
	return true;
}

void ObsServiceGuard::restore()
{
	if (m_ready)
		m_ready->store(false);
	if (m_service && obs_frontend_get_streaming_service() == m_service && m_previous) {
		obs_frontend_set_streaming_service(m_previous);
		obs_frontend_save_streaming_service();
	}
	if (m_service)
		obs_service_release(m_service);
	if (m_previous)
		obs_service_release(m_previous);
	m_service = nullptr;
	m_previous = nullptr;
	m_ready.reset();
}
} // namespace Core
