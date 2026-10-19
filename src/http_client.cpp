#include "http_client.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <cstring>
#include <exception>
#include <mutex>
#include <queue>
#include <string_view>
#include <thread>
#include <chrono>

#include <curl/curl.h>

#include "plugin_utils.hpp"
#include "core/login_utils.hpp"

namespace Http {
namespace {
constexpr size_t MAX_RESPONSE_SIZE = 4 * 1024 * 1024;

struct WriteContext {
	std::string *data;
	bool overflow = false;
};

std::queue<std::function<void()>> taskQueue;
std::mutex queueMutex;
std::condition_variable queueCondition;
std::atomic_bool stopWorker{false};
std::thread workerThread;
bool initialized = false;
std::mutex proxyMutex;
bool proxyRequired = false;
Proxy::Endpoint proxyEndpoint;

size_t writeCallback(void *contents, size_t size, size_t count, void *userData)
{
	auto *context = static_cast<WriteContext *>(userData);
	if (size != 0 && count > MAX_RESPONSE_SIZE / size) {
		context->overflow = true;
		return 0;
	}
	const size_t bytes = size * count;
	if (bytes > MAX_RESPONSE_SIZE - (std::min)(context->data->size(), MAX_RESPONSE_SIZE)) {
		context->overflow = true;
		return 0;
	}
	context->data->append(static_cast<char *>(contents), bytes);
	return bytes;
}

bool startsWithCaseInsensitive(std::string_view value, std::string_view prefix)
{
	return value.size() >= prefix.size() &&
	       std::equal(prefix.begin(), prefix.end(), value.begin(), [](char left, char right) {
		       return std::tolower(static_cast<unsigned char>(left)) ==
			      std::tolower(static_cast<unsigned char>(right));
	       });
}

size_t headerCallback(char *buffer, size_t size, size_t count, void *userData)
{
	const size_t bytes = size * count;
	auto *cookies = static_cast<std::string *>(userData);
	const std::string_view header(buffer, bytes);
	static constexpr std::string_view prefix = "set-cookie:";
	if (!startsWithCaseInsensitive(header, prefix))
		return bytes;

	std::string cookie(header.substr(prefix.size()));
	const size_t first = cookie.find_first_not_of(" \t");
	if (first != std::string::npos)
		cookie.erase(0, first);
	const size_t end = cookie.find(';');
	if (end != std::string::npos)
		cookie.resize(end);
	while (!cookie.empty() && (cookie.back() == '\r' || cookie.back() == '\n'))
		cookie.pop_back();
	if (cookie.empty() || cookie.find('=') == std::string::npos)
		return bytes;
	if (!cookies->empty())
		*cookies += "; ";
	*cookies += cookie;
	return bytes;
}

int transferProgress(void *data, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
	const auto *endpoint = static_cast<const Proxy::Endpoint *>(data);
	return stopWorker.load(std::memory_order_relaxed) || (endpoint && !endpoint->ready()) ? 1 : 0;
}

HttpResponse performRequest(const std::string &url, const std::string *postData,
			    const std::vector<std::string> &headers, long timeoutMs,
			    const Proxy::Endpoint *explicitEndpoint = nullptr, bool loginTicket = false)
{
	HttpResponse response;
	if (loginTicket && !Core::trustedLoginUrl(url)) {
		response.data = "登录跳转地址不受信任，已停止获取登录凭据";
		return response;
	}
	bool useProxy;
	Proxy::Endpoint endpoint;
	if (explicitEndpoint) {
		useProxy = true;
		endpoint = *explicitEndpoint;
	} else {
		std::lock_guard<std::mutex> lock(proxyMutex);
		useProxy = proxyRequired;
		endpoint = proxyEndpoint;
	}
	if (useProxy && !endpoint.ready()) {
		response.data = "代理未就绪；已阻止 B 站接口直连，请检查代理设置。";
		return response;
	}
	CURL *curl = curl_easy_init();
	if (!curl) {
		response.data = "CURL 初始化失败";
		return response;
	}

	std::string responseData;
	std::string responseCookies;
	WriteContext writeContext{&responseData};
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	const std::string proxy = "socks5h://127.0.0.1:" + std::to_string(endpoint.port);
	const std::string proxyUser = endpoint.user.toStdString(), proxyPassword = endpoint.password.toStdString();
	curl_easy_setopt(curl, CURLOPT_PROXY, useProxy ? proxy.c_str() : "");
	if (useProxy) {
		curl_easy_setopt(curl, CURLOPT_NOPROXY, "");
		curl_easy_setopt(curl, CURLOPT_PROXYUSERNAME, proxyUser.c_str());
		curl_easy_setopt(curl, CURLOPT_PROXYPASSWORD, proxyPassword.c_str());
	}
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &writeContext);
	curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerCallback);
	curl_easy_setopt(curl, CURLOPT_HEADERDATA, &responseCookies);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeoutMs);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, (std::min)(timeoutMs, 5000L));
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, loginTicket ? 0L : 1L);
	if (loginTicket)
		curl_easy_setopt(curl, CURLOPT_COOKIEFILE,
				 ""); // In-memory, domain-scoped cookies; never a cookie file.
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
#if LIBCURL_VERSION_NUM >= 0x075500
	curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
	curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
#else
	curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
	curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS);
#endif
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, transferProgress);
	curl_easy_setopt(curl, CURLOPT_XFERINFODATA, useProxy ? &endpoint : nullptr);
	if (postData) {
		curl_easy_setopt(curl, CURLOPT_POST, 1L);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData->data());
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(postData->size()));
	}

	curl_slist *headerList = nullptr;
	for (const auto &header : headers)
		headerList = curl_slist_append(headerList, header.c_str());
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);

	CURLcode result = CURLE_OK;
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
	for (int redirects = 0;; ++redirects) {
		if (stopWorker.load(std::memory_order_relaxed) || (useProxy && !endpoint.ready())) {
			result = CURLE_ABORTED_BY_CALLBACK;
			break;
		}
		if (loginTicket) {
			const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
						       deadline - std::chrono::steady_clock::now())
						       .count();
			if (remaining <= 0) {
				result = CURLE_OPERATION_TIMEDOUT;
				break;
			}
			curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(remaining));
			curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS,
					 (std::min)(static_cast<long>(remaining), 5000L));
		}
		result = curl_easy_perform(curl);
		if (!loginTicket || result != CURLE_OK)
			break;
		char *redirect = nullptr;
		curl_easy_getinfo(curl, CURLINFO_REDIRECT_URL, &redirect);
		if (!redirect)
			break;
		if (redirects >= 5) {
			result = CURLE_TOO_MANY_REDIRECTS;
			break;
		}
		if (!Core::trustedLoginUrl(redirect)) {
			result = CURLE_URL_MALFORMAT;
			break;
		}
		const std::string next(redirect);
		curl_easy_setopt(curl, CURLOPT_URL, next.c_str());
		responseData.clear();
	}
	if (useProxy) {
		curl_off_t up = 0, down = 0;
		curl_easy_getinfo(curl, CURLINFO_SIZE_UPLOAD_T, &up);
		curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD_T, &down);
		endpoint.counters->uploaded.fetch_add(static_cast<quint64>((std::max)(up, curl_off_t(0))));
		endpoint.counters->downloaded.fetch_add(static_cast<quint64>((std::max)(down, curl_off_t(0))));
	}
	if (result != CURLE_OK) {
		response.networkError = int(result);
		response.timeout = result == CURLE_OPERATION_TIMEDOUT;
		response.cancelled = result == CURLE_ABORTED_BY_CALLBACK && stopWorker.load(std::memory_order_relaxed);
		if (writeContext.overflow) {
			response.data = "服务器响应超过安全大小限制";
		} else if (response.cancelled) {
			response.data = "请求已取消";
		} else {
			response.data = std::string("网络错误: ") + curl_easy_strerror(result);
		}
	} else {
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
		response.data = std::move(responseData);
		response.cookies = std::move(responseCookies);
	}

	curl_slist_free_all(headerList);
	curl_easy_cleanup(curl);
	return response;
}

void workerLoop()
{
	while (true) {
		std::function<void()> task;
		{
			std::unique_lock<std::mutex> lock(queueMutex);
			queueCondition.wait(lock, [] { return stopWorker.load() || !taskQueue.empty(); });
			if (stopWorker.load())
				return;
			task = std::move(taskQueue.front());
			taskQueue.pop();
		}
		try {
			task();
		} catch (const std::exception &error) {
			obs_log(LOG_ERROR, "Unhandled background task failure: %s", error.what());
		} catch (...) {
			obs_log(LOG_ERROR, "Unhandled background task failure");
		}
	}
}
} // namespace

void HttpClient::setProxy(bool required, const Proxy::Endpoint &endpoint)
{
	std::lock_guard<std::mutex> lock(proxyMutex);
	proxyRequired = required;
	proxyEndpoint = endpoint;
}

bool HttpClient::init()
{
	std::lock_guard<std::mutex> lock(queueMutex);
	if (initialized)
		return true;
	if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
		obs_log(LOG_ERROR, "Failed to initialize CURL");
		return false;
	}
	stopWorker.store(false);
	try {
		workerThread = std::thread(workerLoop);
	} catch (const std::exception &error) {
		obs_log(LOG_ERROR, "Failed to start the background request worker: %s", error.what());
		curl_global_cleanup();
		return false;
	}
	initialized = true;
	return true;
}

void HttpClient::cleanup()
{
	{
		std::lock_guard<std::mutex> lock(queueMutex);
		if (!initialized)
			return;
		stopWorker.store(true);
		std::queue<std::function<void()>> empty;
		taskQueue.swap(empty);
	}
	queueCondition.notify_all();
	if (workerThread.joinable())
		workerThread.join();
	curl_global_cleanup();
	std::lock_guard<std::mutex> lock(queueMutex);
	initialized = false;
}

bool HttpClient::enqueue(std::function<void()> task)
{
	{
		std::lock_guard<std::mutex> lock(queueMutex);
		if (!initialized || stopWorker.load())
			return false;
		taskQueue.push(std::move(task));
	}
	queueCondition.notify_one();
	return true;
}

HttpResponse HttpClient::get(const std::string &url, const std::vector<std::string> &headers, long timeoutMs)
{
	return performRequest(url, nullptr, headers, timeoutMs);
}

HttpResponse HttpClient::getLoginTicket(const std::string &url, const std::vector<std::string> &headers, long timeoutMs)
{
	return performRequest(url, nullptr, headers, timeoutMs, nullptr, true);
}

HttpResponse HttpClient::getViaProxy(const std::string &url, const Proxy::Endpoint &endpoint,
				     const std::vector<std::string> &headers, long timeoutMs)
{
	return performRequest(url, nullptr, headers, timeoutMs, &endpoint);
}

HttpResponse HttpClient::post(const std::string &url, const std::string &data, const std::vector<std::string> &headers,
			      long timeoutMs)
{
	return performRequest(url, &data, headers, timeoutMs);
}
} // namespace Http
