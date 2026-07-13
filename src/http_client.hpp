#pragma once
#include <functional>
#include <string>
#include <vector>

namespace Http {
struct HttpResponse {
	long status = 0;
	std::string data;
	std::string cookies;
	bool timeout = false;
	bool cancelled = false;
};

class HttpClient {
public:
	static bool init();
	static void cleanup();
	static bool enqueue(std::function<void()> task);
	static HttpResponse get(const std::string &url, const std::vector<std::string> &headers = {},
				long timeout_ms = 10000);
	static HttpResponse post(const std::string &url, const std::string &data,
				 const std::vector<std::string> &headers = {}, long timeout_ms = 10000);
};
} // namespace Http
