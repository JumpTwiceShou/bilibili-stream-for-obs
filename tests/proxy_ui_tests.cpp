#include "ui/proxy_dialog.hpp"
#include "http_client.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFocusEvent>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QElapsedTimer>
#include <QThread>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QWheelEvent>
#include <iostream>

int main(int argc, char **argv)
{
	QApplication app(argc, argv);
	if (!Http::HttpClient::init())
		return 1;
	struct HttpCleanup {
		~HttpCleanup() { Http::HttpClient::cleanup(); }
	} httpCleanup;
	if (argc > 2 && QString::fromLocal8Bit(argv[2]) == "--vpn-probe-stdin") {
		const std::string input((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
		const auto data = QJsonDocument::fromJson(QByteArray::fromStdString(input)).object();
		Proxy::Settings settings;
		settings.enabled = true;
		settings.backend = "openvpn";
		settings.ovpnProfile = data["profile"].toString();
		settings.ovpnUsername = data["username"].toString();
		settings.ovpnPassword = data["password"].toString();
		const QString core = QString::fromLocal8Bit(argv[1]);
		Proxy::Runtime runtime(core);
		runtime.start(settings);
		QElapsedTimer deadline;
		deadline.start();
		while (runtime.starting() && deadline.elapsed() < 65000) {
			QApplication::processEvents();
			QThread::msleep(5);
		}
		if (!runtime.endpoint().ready()) {
			std::cout << "UI_VPN_CONNECT_FAILED\n";
			return 1;
		}
		bool passed = false;
		QTimer click, inspect;
		click.setSingleShot(true);
		QObject::connect(&click, &QTimer::timeout, &app, [&] {
			auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
			if (!dialog)
				return;
			for (auto *button : dialog->findChildren<QPushButton *>())
				if (button->text() == QStringLiteral("检测连接、延迟和出口 IP"))
					button->click();
		});
		QObject::connect(&inspect, &QTimer::timeout, &app, [&] {
			auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
			if (!dialog)
				return;
			for (auto *label : dialog->findChildren<QLabel *>()) {
				if (label->text().startsWith(QStringLiteral("连接成功")) ||
				    label->text().startsWith(QStringLiteral("检测失败")) ||
				    label->text().contains(QStringLiteral("代理核心缺失"))) {
					passed = label->text().startsWith(QStringLiteral("连接成功"));
					std::cout << "UI_PROBE " << label->text().toStdString() << '\n';
					dialog->reject();
					return;
				}
			}
		});
		QTimer::singleShot(85000, &app, [&] {
			if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
				dialog->reject();
		});
		click.start(80);
		inspect.start(100);
		UI::proxyDialog(
			nullptr, settings, runtime, data["require_reuse"].toBool() ? QString() : core,
			[] { return false; }, [](const Proxy::Settings &, QString &) { return true; });
		std::cout << "UI_ACTIVE_TUNNEL_READY=" << runtime.endpoint().ready() << '\n';
		return passed && runtime.endpoint().ready() ? 0 : 1;
	}
	QTimer::singleShot(12000, &app, [] {
		std::cerr << "FAIL: UI test watchdog\n";
		std::exit(1);
	});
	int failures = 0, applied = 0;
	auto check = [&](bool value, const char *message) {
		if (!value) {
			++failures;
			std::cerr << "FAIL: " << message << '\n';
		}
	};
	Proxy::Settings stored;
	stored.stream = true;
	stored.uri = "ss://YWVzLTI1Ni1nY206dGVzdA==@node.example:8388";
	Proxy::Runtime runtime{QString()};
	auto capture = [](QDialog *dialog, const char *path) {
		QElapsedTimer animation;
		animation.start();
		while (animation.elapsed() < 180) {
			QApplication::processEvents();
			QThread::msleep(5);
		}
		dialog->grab().save(QString::fromLocal8Bit(path));
	};
	auto apply = [&](const Proxy::Settings &settings, QString &) {
		stored = settings;
		++applied;
		return true;
	};
	QTimer::singleShot(50, &app, [&] {
		auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
		check(dialog != nullptr, "dialog opens");
		if (!dialog)
			return;
		auto toggles = dialog->findChildren<QCheckBox *>();
		check(toggles.size() == 5, "five accessible pill switches");
		auto *buttons = dialog->findChild<QDialogButtonBox *>();
		check(!buttons->button(QDialogButtonBox::Save), "no save button needed");
		auto *result = dialog->findChild<QLabel *>("proxyTestResult");
		auto *saveStatus = dialog->findChild<QLabel *>("proxySaveStatus");
		result->setText("fixture detection result");
		dialog->findChild<QCheckBox *>("proxyEnabled")->setChecked(true);
		check(stored.enabled && applied == 1, "master saves immediately");
		check(result->text() == "fixture detection result" &&
			      saveStatus->text().contains(QStringLiteral("已自动保存")),
		      "saving leaves the diagnostic result visible");
		dialog->findChild<QCheckBox *>("proxyApi")->setChecked(false);
		check(!stored.api && stored.stream && applied == 2, "API and stream scopes save independently");
		dialog->findChild<QCheckBox *>("proxyEnabled")->setChecked(false);
		check(!stored.enabled && !stored.api && stored.stream && applied == 3, "master retains chosen scopes");
		check(dialog->findChild<QLabel *>("proxyKeepNote")->text().contains(QStringLiteral("代理已关闭")),
		      "disabled master overrides retained stream-scope explanation");
		check(!dialog->findChild<QPushButton *>("proxyReconnect")->isEnabled(),
		      "disabled proxy cannot reconnect");
		if (argc > 1)
			capture(dialog, argv[1]);
		dialog->reject();
	});
	UI::proxyDialog(nullptr, stored, runtime, {}, [] { return false; }, apply);
	QTimer::singleShot(50, &app, [&] {
		auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
		for (auto *toggle : dialog->findChildren<QCheckBox *>())
			check(!toggle->isEnabled(), "streaming locks switches");
		check(!dialog->findChild<QLabel *>("proxyLockNotice")->isHidden(),
		      "locked settings have a fixed visible explanation");
		check(!dialog->findChild<QLineEdit *>("ssUri")->isEnabled(), "streaming locks node fields");
		dialog->reject();
	});
	UI::proxyDialog(nullptr, stored, runtime, {}, [] { return true; }, apply);
	check(applied == 3, "locked dialog never applies");
	QTimer::singleShot(50, &app, [&] {
		auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
		auto *toggle = dialog->findChild<QCheckBox *>("proxyEnabled");
		toggle->setChecked(true);
		check(!toggle->isChecked(), "save failure rolls back switch");
		dialog->reject();
	});
	UI::proxyDialog(
		nullptr, stored, runtime, {}, [] { return false; },
		[](const Proxy::Settings &, QString &error) {
			error = "fixture write failure";
			return false;
		});
	Proxy::Settings apiOnly;
	apiOnly.enabled = true;
	apiOnly.uri = "ss://YWVzLTI1Ni1nY206dGVzdA==@node.example:8388";
	QTimer::singleShot(50, &app, [&] {
		auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
		auto *keep = dialog->findChild<QCheckBox *>("keepConnected");
		check(keep && keep->isEnabled() && !keep->isChecked(), "API-only defaults to on-demand connections");
		keep->setChecked(true);
		check(stored.keepConnected && !stored.onDemandApi(), "keep-connected switch saves immediately");
		auto *affiliate = dialog->findChild<QLabel *>("vpnAffiliate");
		check(affiliate && affiliate->text().contains("r3011477236") &&
			      affiliate->text().contains(QStringLiteral("返佣")),
		      "affiliate relationship is disclosed");
		check(affiliate->textInteractionFlags().testFlag(Qt::LinksAccessibleByKeyboard) &&
			      affiliate->focusPolicy() != Qt::NoFocus,
		      "affiliate link supports keyboard navigation");
		auto *backend = dialog->findChild<QComboBox *>("proxyBackend");
		const int selected = backend->currentIndex();
		QWheelEvent wheel(QPointF(10, 10), QPointF(10, 10), QPoint(), QPoint(0, -120), Qt::NoButton,
				  Qt::NoModifier, Qt::NoScrollPhase, false);
		QApplication::sendEvent(backend, &wheel);
		check(backend->currentIndex() == selected, "page scrolling cannot change the proxy backend");
#ifndef PERSONAL_OVPN_BUNDLED
		check(dialog->findChild<QComboBox *>("vpnPreset")->count() == 1,
		      "public build has no personal VPN presets");
#endif
		dialog->reject();
	});
	UI::proxyDialog(nullptr, apiOnly, runtime, {}, [] { return false; }, apply);
	Proxy::Settings vpn;
	vpn.backend = "openvpn";
	vpn.ovpnProfile =
		"client\ndev tun\nproto udp\nremote fixture.example 1194\nauth-user-pass\n<ca>\nfixture-ca\n</ca>\n";
	QTimer::singleShot(50, &app, [&] {
		auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
		auto *username = dialog->findChild<QLineEdit *>("vpnUsername");
		auto *password = dialog->findChild<QLineEdit *>("vpnPassword");
		check(password->echoMode() == QLineEdit::Password, "VPN password remains masked");
		username->setText("fixture-user");
		password->setText("fixture-password");
		dialog->findChild<QCheckBox *>("proxyEnabled")->setChecked(true);
		check(stored.enabled && stored.backend == "openvpn" && stored.ovpnUsername == "fixture-user",
		      "VPN settings autosave");
		username->setText("edited-user");
		if (argc > 2)
			capture(dialog, argv[2]);
		dialog->resize(560, 380);
		QApplication::processEvents();
		auto *scroll = dialog->findChild<QScrollArea *>();
		check(scroll->horizontalScrollBar()->maximum() == 0, "narrow window needs no horizontal scrolling");
		auto *close = dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Close);
		check(dialog->rect().contains(QRect(close->mapTo(dialog, QPoint()), close->size())),
		      "close remains visible outside the scroll area");
		dialog->reject();
	});
	UI::proxyDialog(nullptr, vpn, runtime, {}, [] { return false; }, apply);
	check(stored.ovpnUsername == "edited-user", "close flushes pending text autosave");
	QTimer::singleShot(50, &app, [&] {
		auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
		check(dialog->findChild<QLineEdit *>("vpnUsername")->text() == "edited-user",
		      "reopen restores saved settings");
		dialog->reject();
	});
	UI::proxyDialog(nullptr, stored, runtime, {}, [] { return false; }, apply);
	bool busy = false;
	QTimer::singleShot(50, &app, [&] {
		auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
		auto *username = dialog->findChild<QLineEdit *>("vpnUsername");
		username->setText("deferred-fixture-user");
		busy = true;
		QTimer::singleShot(0, &app, [&] {
			auto *warning = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
			check(warning != nullptr, "closing locked dirty settings explains unsaved changes");
			if (warning)
				warning->reject();
		});
		dialog->reject();
		check(dialog->isVisible(), "pending edits cannot be silently discarded while settings are locked");
		check(stored.ovpnUsername != "deferred-fixture-user", "locked operation retains the active settings");
		busy = false;
		QTimer::singleShot(1100, &app, [&, dialog] {
			check(stored.ovpnUsername == "deferred-fixture-user",
			      "unlock applies deferred edits automatically");
			dialog->reject();
		});
	});
	UI::proxyDialog(nullptr, stored, runtime, {}, [&] { return busy; }, apply);
	QTimer::singleShot(50, &app, [&] {
		auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
		dialog->findChild<QLineEdit *>("vpnUsername")->setText("test-before-debounce");
		check(dialog->findChild<QLabel *>("proxyTestResult")->text().contains(QStringLiteral("尚未检测")),
		      "editing a node invalidates the previous diagnostic result");
		check(dialog->findChild<QPushButton *>("proxyReconnect")->isHidden(),
		      "on-demand mode has one clear connection test action");
		dialog->findChild<QPushButton *>("proxyTest")->click();
		check(stored.ovpnUsername == "test-before-debounce",
		      "test flushes pending edits before locking controls");
		dialog->reject();
	});
	UI::proxyDialog(nullptr, stored, runtime, {}, [] { return false; }, apply);
	QTimer::singleShot(50, &app, [&] {
		auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
		auto *password = dialog->findChild<QLineEdit *>("vpnPassword");
		password->setFocus();
		password->setText("partially-typed-fixture");
		const int before = applied;
		QTimer::singleShot(950, &app, [&, dialog, password, before] {
			check(applied == before,
			      "typing a password does not apply partial credentials after a short pause");
			QKeyEvent typed(QEvent::KeyPress, Qt::Key_X, Qt::NoModifier, "x");
			QApplication::sendEvent(password, &typed);
			QFocusEvent background(QEvent::FocusOut, Qt::ActiveWindowFocusReason);
			QApplication::sendEvent(password, &background);
			check(applied == before, "switching to a password manager does not save partial credentials");
			QFocusEvent foreground(QEvent::FocusIn, Qt::ActiveWindowFocusReason);
			QApplication::sendEvent(password, &foreground);
			QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
			QApplication::sendEvent(password, &enter);
			check(dialog->isVisible(),
			      "Enter in a credential field does not close the dialog or start a test");
			check(stored.ovpnPassword == "partially-typed-fixturex",
			      "finishing input saves credentials automatically");
			dialog->reject();
		});
	});
	UI::proxyDialog(nullptr, stored, runtime, {}, [] { return false; }, apply);
	QTimer::singleShot(50, &app, [&] {
		auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
		dialog->findChild<QLineEdit *>("vpnUsername")->setText("unsaved-fixture-user");
		dialog->findChild<QPushButton *>("proxyTest")->click();
		check(dialog->findChild<QLabel *>("proxyTestResult")->text().contains(QStringLiteral("未进行检测")),
		      "failed persistence prevents testing a configuration that was not applied");
		auto answer = [&](bool discard) {
			QTimer::singleShot(0, &app, [&, discard] {
				auto *warning = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
				check(warning != nullptr, "failed autosave is visible before closing");
				if (!warning)
					return;
				if (!discard) {
					warning->reject();
					return;
				}
				for (auto *button : warning->buttons())
					if (button->text() == QStringLiteral("放弃修改并关闭"))
						button->click();
			});
		};
		answer(false);
		dialog->reject();
		check(dialog->isVisible(), "returning from failed save keeps edits open");
		answer(true);
		dialog->reject();
		check(!dialog->isVisible(), "explicit discard always permits closing");
	});
	UI::proxyDialog(
		nullptr, stored, runtime, {}, [] { return false; },
		[](const Proxy::Settings &, QString &error) {
			error = "fixture write failure";
			return false;
		});
	std::cout << "UI tests: " << failures << " failure(s)\n";
	return failures ? 1 : 0;
}
