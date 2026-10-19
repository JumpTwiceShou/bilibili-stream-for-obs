#pragma once
#include <functional>
#include <string>
#include <vector>
#include "proxy/proxy_runtime.hpp"

namespace Http {
struct HttpResponse {
	long status = 0;
	std::string data;
	std::string cookies;
	bool timeout = false;
	bool cancelled = false;
	int networkError = 0;
};

class HttpClient {
public:
	static bool init();
	static void cleanup();
	static bool enqueue(std::function<void()> task);
	static void setProxy(bool required, const Proxy::Endpoint &endpoint);
	static HttpResponse get(const std::string &url, const std::vector<std::string> &headers = {},
				long timeout_ms = 10000);
	static HttpResponse getLoginTicket(const std::string &url, const std::vector<std::string> &headers = {},
					   long timeout_ms = 10000);
	static HttpResponse getViaProxy(const std::string &url, const Proxy::Endpoint &endpoint,
					const std::vector<std::string> &headers = {}, long timeout_ms = 10000);
	static HttpResponse post(const std::string &url, const std::string &data,
				 const std::vector<std::string> &headers = {}, long timeout_ms = 10000);
};
} // namespace Http
