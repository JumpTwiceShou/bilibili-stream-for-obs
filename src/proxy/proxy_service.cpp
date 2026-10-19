#include "proxy_service.hpp"
#include <obs-frontend-api.h>
#include <mutex>

namespace Proxy {
namespace {
struct State {
	std::mutex mutex;
	std::string url, key;
	Endpoint endpoint;
	std::shared_ptr<std::atomic_bool> ready = std::make_shared<std::atomic_bool>(false);
};
std::shared_ptr<State> pending;
using Handle = std::shared_ptr<State>;
State &state(void *data)
{
	return **static_cast<Handle *>(data);
}
bool ready(void *data)
{
	std::lock_guard<std::mutex> lock(state(data).mutex);
	return state(data).ready->load() && state(data).endpoint.ready();
}
const char *connectionInfo(void *data, bool key)
{
	std::lock_guard<std::mutex> lock(state(data).mutex);
	thread_local std::string urlValue, keyValue;
	auto &value = key ? keyValue : urlValue;
	value = key ? state(data).key : state(data).url;
	return value.c_str();
}
} // namespace
void registerService()
{
	obs_service_info info{};
	info.id = "bilibili_personal_proxy";
	info.get_name = [](void *) {
		return "Bilibili Proxy (SS / OpenVPN)";
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
		return connectionInfo(data, false);
	};
	info.get_key = [](void *data) {
		return connectionInfo(data, true);
	};
	info.get_connect_info = [](void *data, uint32_t type) -> const char * {
		if (type == OBS_SERVICE_CONNECT_INFO_SERVER_URL)
			return connectionInfo(data, false);
		if (type == OBS_SERVICE_CONNECT_INFO_STREAM_KEY)
			return connectionInfo(data, true);
		return nullptr;
	};
	info.can_try_to_connect = ready;
	info.initialize = [](void *data, obs_output_t *output) {
		if (ready(data))
			return true;
		obs_output_set_last_error(output, "Bilibili 代理不可用，请从插件菜单重新准备直播。");
		return false;
	};
	obs_register_service(&info);
}
ServiceGuard::~ServiceGuard()
{
	restore();
}
bool ServiceGuard::ownsCurrent() const
{
	return m_service && obs_frontend_get_streaming_service() == m_service;
}
bool ServiceGuard::install(const QString &url, const std::string &key, const Endpoint &endpoint, QString &error)
{
	if (obs_frontend_streaming_active()) {
		if (ownsCurrent() && m_update) {
			m_update(url, key, endpoint);
			return !url.isEmpty() && endpoint.ready();
		}
		error = QStringLiteral("请先停止 OBS 推流。");
		return false;
	}
	restore();
	m_previous = obs_service_get_ref(obs_frontend_get_streaming_service());
	pending = std::make_shared<State>();
	pending->url = url.toStdString();
	pending->key = key;
	pending->endpoint = endpoint;
	m_ready = pending->ready;
	m_update = [state = pending](const QString &newUrl, const std::string &newKey, const Endpoint &newEndpoint) {
		std::lock_guard<std::mutex> lock(state->mutex);
		state->url = newUrl.toStdString();
		state->key = newKey;
		state->endpoint = newEndpoint;
		state->ready->store(!newUrl.isEmpty() && newEndpoint.ready());
	};
	m_ready->store(!url.isEmpty() && endpoint.ready());
	// Secrets live only in instance data, never in OBS's serializable settings.
	m_service = obs_service_create("bilibili_personal_proxy", "Bilibili personal proxy", nullptr, nullptr);
	pending.reset();
	if (!m_service) {
		restore();
		error = QStringLiteral("无法创建 OBS 代理推流服务。");
		return false;
	}
	obs_frontend_set_streaming_service(m_service);
	return true;
}
void ServiceGuard::invalidate()
{
	if (m_ready)
		m_ready->store(false);
}
void ServiceGuard::restore()
{
	invalidate();
	if (ownsCurrent() && m_previous) {
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
	m_update = {};
}
} // namespace Proxy
