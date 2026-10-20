#include "risk_auth_dialog.hpp"
#include "core/api_utils.hpp"
#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QPointer>
#include <QTimer>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QUuid>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#include <shlwapi.h>
#include <wrl.h>
#include <WebView2.h>
#if defined(_MSC_VER) && !defined(__clang__)
// Qt defines a fallback __has_attribute; this SDK header checks only its existence.
#pragma push_macro("__has_attribute")
#undef __has_attribute
#endif
#include <WebView2EnvironmentOptions.h>
#if defined(_MSC_VER) && !defined(__clang__)
#pragma pop_macro("__has_attribute")
#endif
#endif

namespace UI {
#ifdef _WIN32
namespace {
using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
constexpr auto pageUrl = L"https://link.bilibili.com/p/center/index?obs_face_auth=1";

// Chromium cannot authenticate to a SOCKS5 proxy. This loopback CONNECT adapter delegates
// SOCKS authentication/DNS to Qt and never switches to a direct connection.
class SdkTunnel : public QTcpServer {
public:
	SdkTunnel(const Proxy::Endpoint &endpoint, QObject *parent) : QTcpServer(parent), m_endpoint(endpoint)
	{
		connect(this, &QTcpServer::newConnection, this, [this] {
			while (auto *client = nextPendingConnection()) {
				if (!m_endpoint.ready() || findChildren<QTcpSocket *>().size() > 64) {
					client->abort();
					client->deleteLater();
					continue;
				}
				auto buffer = std::make_shared<QByteArray>();
				connect(client, &QTcpSocket::readyRead, client, [this, client, buffer] {
					if (client->property("forwarding").toBool())
						return;
					*buffer += client->readAll();
					if (buffer->size() > 16384) {
						client->abort();
						return;
					}
					if (!buffer->contains("\r\n\r\n"))
						return;
					client->setProperty("forwarding", true);
					const auto fields = buffer->left(buffer->indexOf("\r\n")).split(' ');
					const QUrl target("https://" + QString::fromLatin1(fields.value(1)));
					const auto host = target.host().toLower();
					bool trusted = false;
					for (const auto *domain : {"bilibili.com", "hdslb.com", "biligame.com",
								   "geetest.com", "geetestid.com"})
						trusted |= host == QLatin1String(domain) ||
							   host.endsWith('.' + QString::fromLatin1(domain));
					if (fields.size() != 3 || fields[0] != "CONNECT" || target.port() != 443 ||
					    !target.userInfo().isEmpty() || !trusted || !m_endpoint.ready()) {
						client->abort();
						return;
					}
					auto *remote = new QTcpSocket(client);
					remote->setProxy(m_endpoint.networkProxy());
					connect(remote, &QTcpSocket::connected, client, [client] {
						client->write("HTTP/1.1 200 Connection Established\r\n\r\n");
					});
					connect(client, &QTcpSocket::readyRead, remote, [this, client, remote] {
						if (!m_endpoint.ready()) {
							client->abort();
							return;
						}
						const auto bytes = client->readAll();
						m_endpoint.counters->uploaded +=
							static_cast<unsigned long long>(bytes.size());
						remote->write(bytes);
					});
					connect(remote, &QTcpSocket::readyRead, client, [this, client, remote] {
						if (!m_endpoint.ready()) {
							client->abort();
							return;
						}
						const auto bytes = remote->readAll();
						m_endpoint.counters->downloaded +=
							static_cast<unsigned long long>(bytes.size());
						client->write(bytes);
					});
					connect(remote, &QTcpSocket::disconnected, client,
						&QTcpSocket::disconnectFromHost);
					connect(remote, &QTcpSocket::errorOccurred, client,
						[client] { client->abort(); });
					remote->connectToHost(host, 443);
				});
				connect(client, &QTcpSocket::disconnected, client, &QObject::deleteLater);
				QTimer::singleShot(10000, client, [client] {
					auto *remote = client->findChild<QTcpSocket *>();
					if (!remote || remote->state() != QAbstractSocket::ConnectedState)
						client->abort();
				});
			}
		});
	}

private:
	Proxy::Endpoint m_endpoint;
};

class RiskDialog : public QDialog {
public:
#ifdef BILIBILI_RISK_AUTH_TEST
	QString testScenario;
#endif
	RiskDialog(QWidget *parent, const std::string &cookies, const std::string &voucher, bool proxyRequired,
		   const Proxy::Endpoint &endpoint)
		: QDialog(parent),
		  m_cookies(cookies),
		  m_voucher(voucher),
		  m_proxyRequired(proxyRequired),
		  m_endpoint(endpoint),
		  m_nonce(QUuid::createUuid().toString(QUuid::WithoutBraces))
	{
		setWindowTitle(QStringLiteral("Bilibili 官方安全验证"));
		resize(650, 720);
		auto *layout = new QVBoxLayout(this);
		m_status = new QLabel(QStringLiteral("正在加载 B 站官方验证，请在窗口内扫码完成本次验证。"), this);
		m_status->setWordWrap(true);
		layout->addWidget(m_status);
		m_surface = new QWidget(this);
		m_surface->setMinimumSize(500, 500);
		layout->addWidget(m_surface, 1);
		auto *cancel = new QPushButton(QStringLiteral("取消"), this);
		layout->addWidget(cancel);
		connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
		QTimer::singleShot(0, this, [this] { initialize(); });
		QTimer::singleShot(600000, this,
				   [this] { fail(QStringLiteral("官方安全验证超时，请重新尝试开播。")); });
		auto *liveness = new QTimer(this);
		connect(liveness, &QTimer::timeout, this, [this] {
			if (m_proxyRequired && !m_endpoint.ready()) {
				m_view.Reset();
				if (m_controller)
					m_controller->Close();
				m_controller.Reset();
				fail(QStringLiteral("验证期间代理连接已中断，请恢复连接后重新开播。"));
			}
		});
		liveness->start(200);
	}
	~RiskDialog() override
	{
		if (m_controller)
			m_controller->Close();
		m_view.Reset();
		m_controller.Reset();
		m_environment.Reset();
		if (m_comInitialized)
			CoUninitialize();
	}
	QString token, error;

protected:
	void resizeEvent(QResizeEvent *event) override
	{
		QDialog::resizeEvent(event);
		QTimer::singleShot(0, this, [this] { resizeBrowser(); });
	}

private:
	void fail(const QString &message)
	{
		if (!error.isEmpty())
			return;
		error = message;
		m_status->setText(message);
		m_surface->setEnabled(false);
		if (m_controller)
			m_controller->Close();
		m_view.Reset();
		m_controller.Reset();
	}
	void resizeBrowser()
	{
		if (m_controller) {
			RECT bounds{};
			GetClientRect(reinterpret_cast<HWND>(m_surface->winId()), &bounds);
			m_controller->put_Bounds(bounds);
		}
	}
	void initialize()
	{
		if (!m_directory.isValid() || (m_proxyRequired && !m_endpoint.ready())) {
			fail(QStringLiteral("无法创建安全验证环境，或接口代理尚未连接。"));
			return;
		}
		const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
		if (FAILED(com)) {
			fail(QStringLiteral("无法初始化官方验证窗口。"));
			return;
		}
		m_comInitialized = true;
		auto options = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
		QString arguments = QStringLiteral("--no-proxy-server");
		if (m_proxyRequired) {
			auto *tunnel = new SdkTunnel(m_endpoint, this);
			if (!tunnel->listen(QHostAddress::LocalHost)) {
				fail(QStringLiteral("无法创建验证代理连接。"));
				return;
			}
			arguments = QStringLiteral("--proxy-server=http://127.0.0.1:%1 --proxy-bypass-list=<-loopback>")
					    .arg(tunnel->serverPort());
		}
		options->put_AdditionalBrowserArguments(arguments.toStdWString().c_str());
		const QPointer<RiskDialog> guard(this);
		const auto result = CreateCoreWebView2EnvironmentWithOptions(
			nullptr, m_directory.path().toStdWString().c_str(), options.Get(),
			Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
				[guard](HRESULT status, ICoreWebView2Environment *environment) -> HRESULT {
					if (!guard || !guard->error.isEmpty())
						return S_OK;
					if (FAILED(status) || !environment) {
						guard->fail(QStringLiteral(
							"请安装 Microsoft Edge WebView2 Runtime 后重试官方验证。"));
						return S_OK;
					}
					guard->m_environment = environment;
					ComPtr<ICoreWebView2Environment10> environment10;
					ComPtr<ICoreWebView2ControllerOptions> controllerOptions;
					if (FAILED(environment->QueryInterface(IID_PPV_ARGS(&environment10))) ||
					    FAILED(environment10->CreateCoreWebView2ControllerOptions(
						    &controllerOptions))) {
						guard->fail(QStringLiteral("请更新 WebView2 Runtime 后重试。"));
						return S_OK;
					}
					controllerOptions->put_IsInPrivateModeEnabled(TRUE);
					const auto create = environment10->CreateCoreWebView2ControllerWithOptions(
						reinterpret_cast<HWND>(guard->m_surface->winId()),
						controllerOptions.Get(),
						Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
							[guard](HRESULT hr,
								ICoreWebView2Controller *controller) -> HRESULT {
								if (!guard || !guard->error.isEmpty()) {
									if (controller)
										controller->Close();
									return S_OK;
								}
								if (FAILED(hr) || !controller) {
									guard->fail(QStringLiteral(
										"官方验证窗口创建失败。"));
									return S_OK;
								}
								guard->m_controller = controller;
								controller->get_CoreWebView2(&guard->m_view);
								guard->configureBrowser();
								return S_OK;
							})
							.Get());
					if (FAILED(create))
						guard->fail(QStringLiteral("无法创建隐私验证窗口。"));
					return S_OK;
				})
				.Get());
		if (FAILED(result))
			fail(QStringLiteral("无法启动 WebView2，请检查运行时安装。"));
	}
	void configureBrowser()
	{
		if (!error.isEmpty())
			return;
		if (!m_view) {
			fail(QStringLiteral("官方验证浏览器不可用。"));
			return;
		}
		ComPtr<ICoreWebView2Settings> settings;
		m_view->get_Settings(&settings);
		settings->put_AreDevToolsEnabled(FALSE);
		settings->put_AreDefaultContextMenusEnabled(FALSE);
		ComPtr<ICoreWebView2Settings2> settings2;
		if (SUCCEEDED(settings.As(&settings2)))
			settings2->put_UserAgent(QString::fromLatin1(Core::browserUserAgent).toStdWString().c_str());
		ComPtr<ICoreWebView2_2> view2;
		ComPtr<ICoreWebView2CookieManager> manager;
		if (FAILED(m_view.As(&view2)) || FAILED(view2->get_CookieManager(&manager))) {
			fail(QStringLiteral("无法初始化官方验证会话。"));
			return;
		}
		// Only copy the necessary account cookies into the off-the-record profile.
		for (const auto *name :
		     {"SESSDATA", "bili_jct", "DedeUserID", "DedeUserID__ckMd5", "buvid3", "buvid4"}) {
			const auto value = Core::extractCookieValue(m_cookies, name);
			if (value.empty())
				continue;
			ComPtr<ICoreWebView2Cookie> cookie;
			if (FAILED(manager->CreateCookie(QString::fromLatin1(name).toStdWString().c_str(),
							 QString::fromStdString(value).toStdWString().c_str(),
							 L".bilibili.com", L"/", &cookie))) {
				fail(QStringLiteral("验证会话 Cookie 无效，请重新登录。"));
				return;
			}
			cookie->put_IsSecure(TRUE);
			cookie->put_IsHttpOnly(std::strcmp(name, "SESSDATA") == 0);
			manager->AddOrUpdateCookie(cookie.Get());
		}
		const QPointer<RiskDialog> guard(this);
		EventRegistrationToken registration{};
		m_view->add_NavigationStarting(
			Callback<ICoreWebView2NavigationStartingEventHandler>(
				[guard](ICoreWebView2 *, ICoreWebView2NavigationStartingEventArgs *args) -> HRESULT {
					LPWSTR uri = nullptr;
					args->get_Uri(&uri);
					const bool allowed = uri && std::wcscmp(uri, pageUrl) == 0;
					CoTaskMemFree(uri);
					if (!allowed || !guard)
						args->put_Cancel(TRUE);
					return S_OK;
				})
				.Get(),
			&registration);
		m_view->add_NewWindowRequested(
			Callback<ICoreWebView2NewWindowRequestedEventHandler>(
				[](ICoreWebView2 *, ICoreWebView2NewWindowRequestedEventArgs *args) -> HRESULT {
					args->put_Handled(TRUE);
					return S_OK;
				})
				.Get(),
			&registration);
		m_view->AddWebResourceRequestedFilter(pageUrl, COREWEBVIEW2_WEB_RESOURCE_CONTEXT_DOCUMENT);
		m_view->add_WebResourceRequested(
			Callback<ICoreWebView2WebResourceRequestedEventHandler>([guard](ICoreWebView2 *,
											ICoreWebView2WebResourceRequestedEventArgs
												*args) -> HRESULT {
				if (!guard || !guard->error.isEmpty())
					return S_OK;
				// A minimal host at the official center origin, executing the unmodified official SDK.
				const QByteArray html =
					"<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
					"<title>Bilibili official verification</title><body style='background:#fff'></body>";
				ComPtr<IStream> stream;
				stream.Attach(SHCreateMemStream(reinterpret_cast<const BYTE *>(html.constData()),
								static_cast<UINT>(html.size())));
				ComPtr<ICoreWebView2WebResourceResponse> response;
				guard->m_environment->CreateWebResourceResponse(
					stream.Get(), 200, L"OK",
					L"Content-Type: text/html; charset=utf-8\r\nCache-Control: no-store",
					&response);
				args->put_Response(response.Get());
				return S_OK;
			}).Get(),
			&registration);
		m_view->add_WebMessageReceived(
			Callback<ICoreWebView2WebMessageReceivedEventHandler>(
				[guard](ICoreWebView2 *, ICoreWebView2WebMessageReceivedEventArgs *args) -> HRESULT {
					if (!guard || !guard->token.isEmpty() || !guard->error.isEmpty())
						return S_OK;
					LPWSTR source = nullptr, message = nullptr;
					args->get_Source(&source);
					args->get_WebMessageAsJson(&message);
					const bool trusted = source && std::wcscmp(source, pageUrl) == 0;
					const auto json = QJsonDocument::fromJson(
								  message ? QString::fromWCharArray(message).toUtf8()
									  : QByteArray())
								  .object();
					CoTaskMemFree(source);
					CoTaskMemFree(message);
					if (!trusted || json.value("nonce").toString() != guard->m_nonce)
						return S_OK;
					if (json.value("status").toString() == "ready") {
						guard->setProperty("sdkLoaded", true);
						guard->m_status->setText(
							QStringLiteral("官方验证已就绪，请按窗口提示完成本次验证。"));
						return S_OK;
					}
					if (json.value("status").toString() == "success" &&
					    json.value("token").isString() &&
					    !json.value("token").toString().isEmpty() &&
					    json.value("token").toString().size() <= 16384) {
						guard->token = json.value("token").toString();
						QTimer::singleShot(0, guard, [guard] {
							if (guard)
								guard->accept();
						});
					} else if (json.value("status").toString() == "error") {
						QString reason = QStringLiteral(
							"官方验证未完成或加载失败。请取消后重试；不会据此尝试开播。");
						if (json.value("code").isDouble())
							reason += QStringLiteral("（接口代码 %1）")
									  .arg(json.value("code").toInt());
						guard->fail(reason);
					}
					return S_OK;
				})
				.Get(),
			&registration);
		m_view->add_NavigationCompleted(
			Callback<ICoreWebView2NavigationCompletedEventHandler>([guard](ICoreWebView2 *,
										       ICoreWebView2NavigationCompletedEventArgs
											       *args) -> HRESULT {
				if (!guard || !guard->error.isEmpty())
					return S_OK;
				BOOL success = FALSE;
				args->get_IsSuccess(&success);
				if (!success) {
					guard->fail(QStringLiteral("官方验证页面加载失败。"));
					return S_OK;
				}
				QJsonArray options{QJsonObject{{"voucher", QString::fromStdString(guard->m_voucher)},
							       {"nonce", guard->m_nonce}}};
				QString script = QStringLiteral(R"JS((async () => {
const o = %1[0];
const send = (status, token) => chrome.webview.postMessage({nonce:o.nonce,status,token});
try {
 if(!window.CaptchaLoader) await new Promise((resolve,reject) => {
  const s=document.createElement('script');
  s.src='https://s1.hdslb.com/bfs/seed/jinkela/risk-captcha-sdk/CaptchaLoader.js';
  s.onload=resolve;s.onerror=reject;document.head.appendChild(s);
 });
 const verify = await window.CaptchaLoader.load();
 send('ready','');
 const token = await verify({riskParams:{v_voucher:o.voucher}});
 if(typeof token !== 'string' || !token) throw new Error('no completion token');
 send('success',token);
} catch (e) {
 chrome.webview.postMessage({nonce:o.nonce,status:'error',code:Number.isInteger(e && e.code)?e.code:null});
}
})();)JS")
							 .arg(QString::fromUtf8(QJsonDocument(options).toJson(
								 QJsonDocument::Compact)));
#ifdef BILIBILI_RISK_AUTH_TEST
				if (guard->testScenario == "success")
					script.prepend(
						"window.CaptchaLoader={load:async()=>async()=> 'fixture-validated-token'};");
				else if (guard->testScenario == "wrong-nonce")
					script.prepend(
						"window.CaptchaLoader={load:async()=>async()=>{chrome.webview.postMessage({nonce:'forged',status:'success',token:'forged'});return new Promise(()=>{});}};");
				else if (guard->testScenario == "empty")
					script.prepend("window.CaptchaLoader={load:async()=>async()=> ''};");
				else if (guard->testScenario == "drop")
					script.prepend(
						"window.CaptchaLoader={load:async()=>async()=>new Promise(()=>{})};");
#endif
				guard->m_view->ExecuteScript(script.toStdWString().c_str(), nullptr);
				return S_OK;
			}).Get(),
			&registration);
		resizeBrowser();
		m_controller->put_IsVisible(TRUE);
		m_view->Navigate(pageUrl);
	}
	std::string m_cookies, m_voucher;
	bool m_proxyRequired = false, m_comInitialized = false;
	Proxy::Endpoint m_endpoint;
	QString m_nonce;
	QTemporaryDir m_directory;
	QLabel *m_status = nullptr;
	QWidget *m_surface = nullptr;
	ComPtr<ICoreWebView2Environment> m_environment;
	ComPtr<ICoreWebView2Controller> m_controller;
	ComPtr<ICoreWebView2> m_view;
};
} // namespace
#endif

QString officialRiskAuth(QWidget *parent, const std::string &cookies, const std::string &voucher, bool proxyRequired,
			 const Proxy::Endpoint &endpoint, QString &error)
{
#ifdef _WIN32
	RiskDialog dialog(parent, cookies, voucher, proxyRequired, endpoint);
	const auto result = dialog.exec();
	error = dialog.error;
	return result == QDialog::Accepted ? dialog.token : QString();
#else
	(void)parent;
	(void)cookies;
	(void)voucher;
	(void)proxyRequired;
	(void)endpoint;
	error = QStringLiteral("本平台暂不支持这类官方安全验证，请通过 B 站官方开播页面完成后重试。");
	return {};
#endif
}

#if defined(_WIN32) && defined(BILIBILI_RISK_AUTH_TEST)
int runOfficialRiskUiTests(bool online, const Proxy::Endpoint &endpoint)
{
	int failures = 0;
	const bool proxy = endpoint.port != 0;
	const QStringList scenarios = online ? (proxy ? QStringList{"official", "drop"} : QStringList{"official"})
					     : QStringList{"success", "wrong-nonce", "empty", "cancel"};
	for (const auto &scenario : scenarios) {
		endpoint.alive->store(proxy);
		RiskDialog dialog(nullptr, "", "invalid-fixture-voucher", proxy, endpoint);
		dialog.testScenario = scenario;
		QTimer deadline(&dialog);
		deadline.setSingleShot(true);
		QObject::connect(&deadline, &QTimer::timeout, &dialog, &QDialog::reject);
		deadline.start(scenario == "cancel" ? 1 : scenario == "wrong-nonce" ? 5000 : 20000);
		QTimer observer(&dialog);
		QObject::connect(&observer, &QTimer::timeout, &dialog, [&] {
			if (scenario == "drop" && dialog.property("sdkLoaded").toBool())
				endpoint.alive->store(false);
			if (!dialog.error.isEmpty())
				dialog.reject();
		});
		observer.start(50);
		const auto result = dialog.exec();
		const bool passed =
			scenario == "drop"
				? dialog.token.isEmpty() && dialog.error.contains(QStringLiteral("代理连接已中断"))
			: scenario == "success"
				? result == QDialog::Accepted && dialog.token == "fixture-validated-token"
			: scenario == "wrong-nonce" ? dialog.token.isEmpty() && dialog.property("sdkLoaded").toBool()
			: scenario == "cancel"      ? dialog.token.isEmpty()
						    : dialog.token.isEmpty() && dialog.property("sdkLoaded").toBool() &&
							 !dialog.error.isEmpty();
		fprintf(stdout, "%s risk browser scenario %s (SDK loaded=%s)\n", passed ? "PASS" : "FAIL",
			scenario.toUtf8().constData(), dialog.property("sdkLoaded").toBool() ? "yes" : "no");
		failures += !passed;
	}
	if (proxy) {
		const bool counted = endpoint.counters->uploaded.load() > 0 && endpoint.counters->downloaded.load() > 0;
		fprintf(stdout, "%s official SDK traversed authenticated local SOCKS adapter\n",
			counted ? "PASS" : "FAIL");
		failures += !counted;
	}
	return failures ? 1 : 0;
}
#endif
} // namespace UI
