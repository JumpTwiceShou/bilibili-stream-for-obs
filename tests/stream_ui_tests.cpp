#include "ui/dialog_factory.hpp"
#include "ui/native_start_filter.hpp"
#include "ui/toggle_switch.hpp"
#include "ui/stream_routes_dialog.hpp"
#include <QClipboard>
#include <QListWidget>
#include <QApplication>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QTimer>
#include <iostream>

extern "C" {
void *obs_frontend_get_main_window()
{
	return nullptr;
}
const char *obs_module_text(const char *key)
{
	return key;
}
}

int main(int argc, char **argv)
{
	QApplication app(argc, argv);
	int failures = 0;
	auto check = [&](bool value, const char *message) {
		if (!value) {
			++failures;
			std::cerr << "FAIL: " << message << '\n';
		}
	};
	QTimer::singleShot(15000, &app, [] { std::exit(2); });
	QPushButton button;
	button.resize(150, 40);
	bool enabled = true;
	int starts = 0, nativeClicks = 0;
	UI::NativeStartFilter filter(&button, [&] { return enabled; }, [&] { ++starts; }, &app);
	QObject::connect(&button, &QPushButton::clicked, [&] { ++nativeClicks; });
	auto click = [&] {
		QMouseEvent press(QEvent::MouseButtonPress, QPointF(10, 10), QPointF(10, 10), Qt::LeftButton,
				  Qt::LeftButton, Qt::NoModifier);
		QApplication::sendEvent(&button, &press);
		QMouseEvent release(QEvent::MouseButtonRelease, QPointF(10, 10), QPointF(10, 10), Qt::LeftButton,
				    Qt::NoButton, Qt::NoModifier);
		QApplication::sendEvent(&button, &release);
	};
	click();
	check(starts == 1 && nativeClicks == 0, "native gesture waits for Bilibili preparation");
	enabled = false;
	click();
	check(starts == 1 && nativeClicks == 1, "disabled synchronization preserves native OBS behavior");
	enabled = true;
	QKeyEvent key(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
	QApplication::sendEvent(&button, &key);
	check(starts == 2 && nativeClicks == 1, "keyboard activation uses preparation too");
	UI::ToggleSwitch pill("Fixture");
	pill.resize(180, 36);
	pill.show();
	QKeyEvent spaceDown(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
	QApplication::sendEvent(&pill, &spaceDown);
	QApplication::sendEvent(&pill, &key);
	check(pill.isChecked(), "pill retains checkbox keyboard accessibility");

	using FaceStatus = Bili::FaceAuthStatus;
	int stablePolls = 0, stableRetries = 0;
	QString waitingStatus, waitingDetail;
	const bool stableWaiting = UI::DialogFactory::faceAuth(
		nullptr, "https://fixture.example/face",
		[&](auto callback) {
			auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
			auto *status = dialog->findChild<QLabel *>("faceWaitingStatus");
			auto *detail = dialog->findChild<QLabel *>("faceWaitingDetail");
			if (++stablePolls == 1) {
				waitingStatus = status->text();
				waitingDetail = detail->text();
				check(waitingStatus == "Face.Waiting" && waitingDetail == "Face.WaitingHint",
				      "both waiting labels provide guidance from the initial frame");
				auto timers = dialog->findChildren<QTimer *>();
				check(timers.size() == 1 && timers.front()->interval() == 1500,
				      "face status polling retains its 1.5 second interval");
				for (auto *timer : timers)
					timer->setInterval(1);
			}
			// Query errors, pending responses and rejected automatic starts must not change waiting text.
			callback(stablePolls % 3 == 0   ? FaceStatus::Verified
				 : stablePolls % 3 == 1 ? FaceStatus::Unavailable
							: FaceStatus::Pending);
			check(status->text() == waitingStatus && detail->text() == waitingDetail,
			      "all waiting text remains stable across status errors, pending and rejected starts");
			if (stablePolls == 6)
				dialog->reject();
		},
		[&](auto callback) {
			++stableRetries;
			callback(Core::FaceAuthRetryDecision::ContinueWaiting, {}, {});
		});
	check(!stableWaiting && stableRetries == 1,
	      "stable guidance preserves the retry cooldown even when status rapidly alternates");
	UI::DialogFactory::faceAuth(
		nullptr, "https://fixture.example/face", [](auto callback) { callback(FaceStatus::Verified); },
		[&](auto callback) {
			callback(Core::FaceAuthRetryDecision::Failed, {}, "fixture terminal error");
			auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
			check(dialog->findChild<QLabel *>("faceWaitingStatus")->text() == "Dialog.StartFailed" &&
				      dialog->findChild<QLabel *>("faceWaitingDetail")->text() == "fixture terminal error",
			      "terminal failures replace waiting guidance with the actionable error");
			dialog->reject();
		});

	auto speedUp = [&] {
		QTimer::singleShot(0, &app, [] {
			auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
			for (auto *timer : dialog->findChildren<QTimer *>())
				timer->setInterval(1);
		});
	};
	int timeoutAttempts = 0;
	speedUp();
	const bool recovered = UI::DialogFactory::faceAuth(
		nullptr, "https://fixture.example/face", [](auto callback) { callback(FaceStatus::Verified); },
		[&](auto callback) {
			if (++timeoutAttempts != 1) {
				callback(Core::FaceAuthRetryDecision::Started, {}, {});
				return;
			}
			callback(Core::FaceAuthRetryDecision::VersionTimedOut, {}, "fixture version timeout");
			auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
			QTimer::singleShot(30, dialog, [&, dialog] {
				QPushButton *retry = nullptr;
				for (auto *button : dialog->findChildren<QPushButton *>())
					if (button->text() == "Face.ManualRetry")
						retry = button;
				check(timeoutAttempts == 1 && retry && retry->isEnabled() &&
					      dialog->findChild<QLabel *>("faceWaitingDetail")->text() == "fixture version timeout",
				      "version GET timeout leaves a manual retry enabled without triggering more automatic starts");
				if (retry && retry->isEnabled())
					retry->click();
				else
					dialog->reject();
			});
		});
	check(recovered && timeoutAttempts == 2, "manual retry completes in the original face dialog");
	check(UI::DialogFactory::faceAuth(
		      nullptr, "https://fixture.example/face", [](auto callback) { callback(FaceStatus::Verified); },
		      [](auto callback) { callback(Core::FaceAuthRetryDecision::ChallengeChanged, {}, {}); }),
	      "a different authentication challenge closes the room dialog for the official flow");
	int expiredLoginStarts = 0;
	check(UI::DialogFactory::faceAuth(
		      nullptr, "https://fixture.example/face",
		      [](auto callback) { callback(FaceStatus::LoginRequired); },
		      [&](auto) { ++expiredLoginStarts; }, true) && expiredLoginStarts == 0,
	      "login expiry during verification closes the dialog without retrying start");
	check(UI::DialogFactory::faceAuth(
		      nullptr, "https://fixture.example/face", [](auto callback) { callback(FaceStatus::Verified); },
		      [](auto callback) { callback(Core::FaceAuthRetryDecision::LoginRequired, {}, {}); }),
	      "login expiry after a face scan closes the face dialog for the login prompt");
	int polls = 0, retries = 0;
	speedUp();
	const bool staleCompleted = UI::DialogFactory::faceAuth(
		nullptr, "https://fixture.example/face",
		[&](auto callback) {
			++polls;
			auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
			bool stable = false;
			for (auto *label : dialog->findChildren<QLabel *>())
				stable |= label->text() == "Face.Waiting";
			check(stable, "face main status stays stable");
			callback(FaceStatus::Verified);
			if (polls == 10)
				dialog->reject();
		},
		[&](auto callback) {
			++retries;
			callback(Core::FaceAuthRetryDecision::ContinueWaiting, {}, {});
		});
	check(!staleCompleted && retries == 1, "stale face success cannot repeat starts on every poll");
	polls = retries = 0;
	speedUp();
	const bool completed = UI::DialogFactory::faceAuth(
		nullptr, "https://fixture.example/face",
		[&](auto callback) {
			++polls;
			// Reproduce the live failure: early verified, a query failure, then verified forever.
			// The endpoint never returns pending after the first rejected start.
			callback(polls == 2 ? FaceStatus::Unavailable : FaceStatus::Verified);
		},
		[&](auto callback) {
			++retries;
			callback(retries == 1 ? Core::FaceAuthRetryDecision::ContinueWaiting
					      : Core::FaceAuthRetryDecision::Started,
				 {}, {});
		});
	check(completed && retries == 2 && polls > 3,
	      "early positive followed by rejected start resumes automatically after cooldown without manual input");
	polls = retries = 0;
	const bool firstPositive = UI::DialogFactory::faceAuth(
		nullptr, "https://fixture.example/face",
		[&](auto callback) {
			++polls;
			callback(FaceStatus::Verified);
		},
		[&](auto callback) {
			++retries;
			callback(Core::FaceAuthRetryDecision::Started, {}, {});
		});
	check(firstPositive && polls == 1 && retries == 1,
	      "first explicit positive starts without waiting for a second poll");
	QDialog *originalFaceDialog = nullptr;
	int changedPolls = 0, changedRetries = 0;
	speedUp();
	const bool refreshed = UI::DialogFactory::faceAuth(
		nullptr, "https://fixture.example/face",
		[&](auto callback) {
			auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
			if (!originalFaceDialog)
				originalFaceDialog = dialog;
			check(dialog == originalFaceDialog, "a replacement QR keeps the same native dialog");
			bool hasQr = false;
			for (auto *label : dialog->findChildren<QLabel *>())
				hasQr |= !label->pixmap().isNull();
			check(hasQr, "native face dialog renders a QR without an embedded browser");
			callback(++changedPolls == 2 ? FaceStatus::Pending : FaceStatus::Verified);
			if (changedPolls == 2) {
				for (auto *candidate : dialog->findChildren<QPushButton *>())
					if (candidate->text() == "Face.ManualRetry")
						candidate->click();
			}
		},
		[&](auto callback) {
			callback(++changedRetries == 1 ? Core::FaceAuthRetryDecision::ContinueWaiting
						     : Core::FaceAuthRetryDecision::Started,
				 "https://www.bilibili.com/blackboard/live/face-auth-middle.html?source_event=400&mid=42",
				 {});
		});
	check(refreshed && changedRetries == 2,
	      "manual retry bypasses automatic cooldown and replacement QR keeps the same dialog until actual start");
	UI::DialogFactory::FacePollCallback late;
	speedUp();
	UI::DialogFactory::faceAuth(
		nullptr, "https://fixture.example/face",
		[&](auto callback) {
			late = callback;
			qobject_cast<QDialog *>(QApplication::activeModalWidget())->reject();
		},
		[](auto) {});
	late(FaceStatus::Verified);
	check(true, "late response after cancel is harmless");
	const std::string realnameQr = "https://www.bilibili.com/h5/risk-control/realname?t=" + std::string(250, 'a');
	const auto manualRetry = [](QDialog *dialog) -> QPushButton * {
		for (auto *button : dialog->findChildren<QPushButton *>())
			if (button->text() == "Face.ManualRetry")
				return button;
		return nullptr;
	};
	int realnamePolls = 0;
	int realnameStarts = 0;
	speedUp();
	check(UI::DialogFactory::faceAuth(
		      nullptr, realnameQr,
		      [&](auto callback) {
			      auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
			      auto *button = manualRetry(dialog);
			      check(button && !button->isEnabled() && realnameStarts == 0,
				    "realname cannot request a start while confirmation is pending");
			      bool hasFittingQr = false;
			      for (auto *label : dialog->findChildren<QLabel *>())
				      hasFittingQr |= !label->pixmap().isNull() && label->pixmap().width() <= 480;
			      check(hasFittingQr, "realname fits a long official QR into the native dialog");
			      callback(++realnamePolls == 1 ? FaceStatus::Pending : FaceStatus::Verified);
		      },
		      [&](auto callback) {
			      if (++realnameStarts == 2) {
				      callback(Core::FaceAuthRetryDecision::Started, {}, {});
				      return;
			      }
			      callback(Core::FaceAuthRetryDecision::VersionTimedOut, {}, "fixture version timeout");
			      auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
			      QTimer::singleShot(30, dialog, [&, dialog] {
				      auto *button = manualRetry(dialog);
				      check(realnamePolls == 2 && realnameStarts == 1 && button && button->isEnabled(),
					    "verified realname stops polling and keeps manual retry after the version timeout");
				      if (button && button->isEnabled())
					      button->click();
				      else
					      dialog->reject();
			      });
		      },
		      true) && realnameStarts == 2,
	      "realname resumes through the existing start dialog after confirmation and one manual retry");
	realnamePolls = 0;
	speedUp();
	check(!UI::DialogFactory::faceAuth(
		      nullptr, realnameQr,
		      [&](auto callback) {
			      auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
			      if (++realnamePolls > 1) {
				      dialog->reject();
				      return;
			      }
			      callback(FaceStatus::Expired);
			      QTimer::singleShot(30, dialog, [&, dialog] {
				      auto *button = manualRetry(dialog);
				      check(button && !button->isEnabled() &&
						    dialog->findChild<QLabel *>("faceWaitingStatus")->text() ==
							    "Face.ChallengeExpired",
					    "expired realname disables start and asks for a fresh QR");
				      dialog->reject();
			      });
		      },
		      [&](auto) { check(false, "expired realname must not request a start"); }, true) &&
		      realnamePolls == 1,
	      "realname expiration stops status polling");
	Core::StreamRoutes routes{{"Bilibili response", "rtmp://api.example/live", 100},
				  {"OBS overseas", "rtmp://overseas.example/live", 30}};
	int applied = 0;
	QTimer::singleShot(20, &app, [&] {
		auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
		auto *list = dialog->findChild<QListWidget *>("streamRouteList");
		check(list && list->currentRow() == 1, "recommended direct route is selected");
		QMetaObject::invokeMethod(list, "itemClicked", Qt::DirectConnection,
					  Q_ARG(QListWidgetItem *, list->item(1)));
		check(QApplication::clipboard()->text() == routes[1].url, "clicking a route copies only its address");
		dialog->findChild<QPushButton *>("applyStreamRoute")->click();
		check(applied == 1, "one-click apply dispatches selected route");
		for (auto *label : dialog->findChildren<QLabel *>())
			check(!label->text().contains("fixture-stream-key"), "stream key remains hidden");
		dialog->reject();
	});
	UI::streamRoutesDialog(nullptr, routes, 1, "fixture-stream-key", false, true,
			       [&](const QString &address, QString &) {
				       ++applied;
				       return address == routes[1].url;
			       });
	int obsStarts = 0;
	UI::StreamRouteActions actions;
	actions.obsActive = [] {
		return false;
	};
	actions.startObs = [&](const QString &address, QString &) {
		++obsStarts;
		return address == routes[1].url;
	};
	QTimer::singleShot(20, &app, [&] {
		auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
		check(dialog->windowTitle().contains(QStringLiteral("B 站开播成功")),
		      "route title confirms Bilibili start success");
		auto *banner = dialog->findChild<QLabel *>("streamSessionStatus");
		check(banner && banner->text().contains(QStringLiteral("OBS 尚未开始推流")),
		      "banner distinguishes Bilibili from OBS output");
		for (const auto &argument : app.arguments())
			if (argument.startsWith("--capture-route-dialog="))
				check(dialog->grab().save(argument.mid(QString("--capture-route-dialog=").size())),
				      "capture route dialog");
		check(!dialog->findChild<QCheckBox *>("autoStartObsSwitch") && obsStarts == 0,
		      "route window does not add an automatic-output switch or start output on open");
		auto *start = dialog->findChild<QPushButton *>("startObsStream");
		check(start && start->text() == QStringLiteral("一键推流") && start->isEnabled() &&
			      !start->autoDefault(),
		      "explicit OBS start is available without becoming an Enter default");
		start->click();
		start->click();
	});
	UI::streamRoutesDialog(
		nullptr, routes, 1, "fixture-stream-key", false, true, [](const QString &, QString &) { return true; },
		actions);
	check(obsStarts == 1, "direct OBS start applies selected route once and closes the route dialog");
	QTimer::singleShot(20, &app, [&] {
		auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
		check(!dialog->findChild<QPushButton *>("startObsStream")->isEnabled(),
		      "historical routes cannot start an unconfirmed session");
		dialog->reject();
	});
	UI::streamRoutesDialog(
		nullptr, routes, 1, "old-key", false, false, [](const QString &, QString &) { return true; }, actions);
	check(obsStarts == 1, "closing historical route dialog never starts OBS");
	actions.startObs = [&](const QString &, QString &error) {
		error = "fixture start rejected";
		return false;
	};
	QTimer::singleShot(20, &app, [&] {
		auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
		dialog->findChild<QPushButton *>("startObsStream")->click();
		check(dialog->isVisible() &&
			      dialog->findChild<QLabel *>("streamRouteStatus")->text() == "fixture start rejected",
		      "rejected OBS start stays open and displays its failure");
		dialog->reject();
	});
	UI::streamRoutesDialog(
		nullptr, routes, 1, "fixture-stream-key", false, true, [](const QString &, QString &) { return true; },
		actions);
	bool sessionActive = true;
	actions.sessionActive = [&] {
		return sessionActive;
	};
	QTimer::singleShot(20, &app, [&] { sessionActive = false; });
	QTimer::singleShot(350, &app, [&] {
		auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
		check(!dialog->findChild<QPushButton *>("startObsStream")->isEnabled() &&
			      !dialog->windowTitle().contains(QStringLiteral("开播成功")),
		      "ended session loses success state and cannot start output");
		dialog->reject();
	});
	UI::streamRoutesDialog(
		nullptr, routes, 1, "fixture-stream-key", false, true, [](const QString &, QString &) { return true; },
		actions);
	sessionActive = true;
	actions.obsActive = [] {
		return true;
	};
	QTimer::singleShot(20, &app, [&] {
		auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
		check(!dialog->findChild<QPushButton *>("startObsStream")->isEnabled(),
		      "active OBS output cannot be started again");
		dialog->reject();
	});
	UI::streamRoutesDialog(
		nullptr, routes, 1, "fixture-stream-key", false, true, [](const QString &, QString &) { return true; },
		actions);
	int roomRequests = 0;
	UI::RoomApplyCallback lateRoom;
	const json11::Json partitions = json11::Json::array{json11::Json::object{
		{"id", "1"},
		{"name", "Fixture"},
		{"list", json11::Json::array{json11::Json::object{{"id", "invalid"}, {"name", "Skip"}},
					     json11::Json::object{{"id", 86}, {"name", "Old"}},
					     json11::Json::object{{"id", "87"}, {"name", "New"}}}}}};
	QTimer::singleShot(0, &app, [&] {
		auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
		auto *title = dialog->findChild<QLineEdit *>("roomTitle");
		auto *areas = dialog->findChild<QComboBox *>("roomArea");
		check(areas->count() == 2 && areas->currentData().toInt() == 86,
		      "room list skips malformed entries and preserves selected ID");
		title->setText(QStringLiteral("A&B +中文"));
		areas->setCurrentIndex(1);
		dialog->findChild<QPushButton *>("applyRoomChanges")->click();
	});
	UI::DialogFactory::roomSettings(
		nullptr, "42", "Old title", 86, 1, partitions,
		[&](const UI::RoomChanges &changes, UI::RoomApplyCallback callback) {
			++roomRequests;
			auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
			auto *apply = dialog->findChild<QPushButton *>("applyRoomChanges");
			check(!apply->isEnabled(), "pending request disables repeat submission");
			apply->click();
			dialog->reject();
			check(dialog->isVisible(), "pending API side effect blocks dialog closure");
			if (roomRequests == 1) {
				check(changes.baseTitle == "Old title" && changes.baseAreaId == 86 &&
					      changes.areaId == 87,
				      "initial room request contains original values");
				QTimer::singleShot(10, &app, [&, callback, dialog, apply] {
					callback({true, false, "fixture-area-denied"});
					check(dialog->isVisible() && apply->isEnabled() &&
						      dialog->findChild<QLineEdit *>("roomTitle")->text() ==
							      QStringLiteral("A&B +中文"),
					      "partial failure retains dialog and edited input");
					check(dialog->findChild<QLabel *>("roomApplyFeedback")
						      ->text()
						      .contains("fixture-area-denied"),
					      "failure appears inside settings dialog");
					apply->click();
				});
			} else {
				check(changes.baseTitle == changes.title && changes.baseAreaId == 86,
				      "retry remembers successful title and retries only changed area");
				lateRoom = callback;
				callback({false, true, {}});
				apply->click();
				check(roomRequests == 2, "successful changes are not submitted again");
				dialog->findChild<QPushButton *>("closeRoomSettings")->click();
			}
		});
	lateRoom({true, true, "late"});
	check(roomRequests == 2, "late room completion after close is harmless");
	int qrPolls = 0, qrSuccesses = 0;
	UI::DialogFactory::QrPollCallback lateQr;
	speedUp();
	UI::DialogFactory::qrLogin(
		nullptr, "https://www.bilibili.com/fixture-qr",
		[&](auto callback) {
			++qrPolls;
			lateQr = callback;
			callback(qrPolls == 1 ? Bili::BiliApi::QrLoginResult::Scanned
					      : Bili::BiliApi::QrLoginResult::Expired,
				 {}, "fixture-status");
			if (qrPolls == 2)
				QTimer::singleShot(10, &app, [] {
					qobject_cast<QDialog *>(QApplication::activeModalWidget())->reject();
				});
		},
		[&](const std::string &) { ++qrSuccesses; });
	lateQr(Bili::BiliApi::QrLoginResult::Success, "fixture-cookies", {});
	check(qrPolls == 2 && qrSuccesses == 0, "expired QR stops polling and late success after close is ignored");
	std::cout << "Stream UI tests: " << failures << " failure(s)\n";
	return failures ? 1 : 0;
}
