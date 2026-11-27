#include "ui/dialog_factory.hpp"

#include <QComboBox>
#include <QDialog>
#include <QElapsedTimer>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <memory>
#include <vector>

#include <obs-frontend-api.h>

#include "core/api_utils.hpp"
#include "core/qr_generator.hpp"
#include "ui/locale.hpp"

namespace UI {
namespace {
class FaceDialog : public QDialog {
public:
	using QDialog::QDialog;
	void reject() override
	{
		if (!property("startPending").toBool())
			QDialog::reject();
	}
};
class RoomDialog : public QDialog {
public:
	using QDialog::QDialog;
	void reject() override
	{
		if (!property("operationPending").toBool())
			QDialog::reject();
	}
};
void configureDialog(QDialog &dialog, const QString &title)
{
	dialog.setWindowTitle(title);
	dialog.setLayout(new QVBoxLayout(&dialog));
}

QVBoxLayout *dialogLayout(QDialog &dialog)
{
	return static_cast<QVBoxLayout *>(dialog.layout());
}

QWidget *defaultParent(QWidget *parent)
{
	return parent ? parent : static_cast<QWidget *>(obs_frontend_get_main_window());
}
} // namespace

void DialogFactory::message(const QString &msg, const QString &title, QWidget *parent)
{
	QDialog dialog(defaultParent(parent));
	configureDialog(dialog, title);
	QLabel *label = new QLabel(msg);
	label->setTextFormat(Qt::PlainText);
	label->setTextInteractionFlags(Qt::TextSelectableByMouse);
	label->setWordWrap(true);
	dialogLayout(dialog)->addWidget(label);
	QPushButton *confirm = new QPushButton(text("Common.Confirm"));
	dialogLayout(dialog)->addWidget(confirm);
	QObject::connect(confirm, &QPushButton::clicked, &dialog, &QDialog::accept);
	dialog.exec();
}

bool DialogFactory::confirm(const QString &msg, const QString &title, QWidget *parent, const QString &acceptText)
{
	QDialog dialog(defaultParent(parent));
	configureDialog(dialog, title);
	QLabel *label = new QLabel(msg);
	label->setTextFormat(Qt::PlainText);
	label->setWordWrap(true);
	dialogLayout(dialog)->addWidget(label);
	QHBoxLayout *buttons = new QHBoxLayout();
	QPushButton *cancel = new QPushButton(text("Common.Cancel"));
	QPushButton *confirm = new QPushButton(acceptText.isEmpty() ? text("Common.Confirm") : acceptText);
	buttons->addWidget(cancel);
	buttons->addWidget(confirm);
	dialogLayout(dialog)->addLayout(buttons);
	QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
	QObject::connect(confirm, &QPushButton::clicked, &dialog, &QDialog::accept);
	return dialog.exec() == QDialog::Accepted;
}

void DialogFactory::qrLogin(QWidget *parent, const std::string &qrData, QrPollRequest requestPoll,
			    std::function<void(const std::string &cookies)> onSuccess)
{
	QDialog dialog(parent);
	configureDialog(dialog, text("Qr.Title"));
	QLabel *qrLabel = new QLabel();
	const QPixmap pixmap = Core::QrGenerator::generate(qrData);
	if (pixmap.isNull()) {
		qrLabel->setText(text("Qr.GenerateFailed"));
	} else {
		qrLabel->setPixmap(pixmap);
	}
	qrLabel->setAlignment(Qt::AlignCenter);
	dialogLayout(dialog)->addWidget(qrLabel);
	dialogLayout(dialog)->addWidget(new QLabel(text("Qr.Instruction")));
	QLabel *statusLabel = new QLabel(text("Qr.Waiting"));
	statusLabel->setTextFormat(Qt::PlainText);
	statusLabel->setAlignment(Qt::AlignCenter);
	statusLabel->setWordWrap(true);
	dialogLayout(dialog)->addWidget(statusLabel);

	struct PollState {
		bool inFlight = false;
		QElapsedTimer elapsed;
	};
	auto state = std::make_shared<PollState>();
	state->elapsed.start();
	QTimer timer(&dialog);
	timer.setInterval(1000);
	QPointer<QDialog> guard(&dialog);

	const auto poll = [guard, state, &timer, statusLabel, requestPoll, onSuccess]() {
		if (!guard || state->inFlight)
			return;
		if (state->elapsed.elapsed() > 180000) {
			timer.stop();
			statusLabel->setText(text("Qr.Expired"));
			return;
		}
		state->inFlight = true;
		requestPoll([guard, state, &timer, statusLabel, onSuccess](Bili::BiliApi::QrLoginResult result,
									   std::string cookies,
									   std::string statusMessage) {
			if (!guard)
				return;
			state->inFlight = false;
			if (!statusMessage.empty())
				statusLabel->setText(QString::fromUtf8(statusMessage));
			if (result == Bili::BiliApi::QrLoginResult::Expired) {
				timer.stop();
			} else if (result == Bili::BiliApi::QrLoginResult::Success) {
				timer.stop();
				if (onSuccess)
					onSuccess(cookies);
				guard->accept();
			}
		});
	};

	QObject::connect(&timer, &QTimer::timeout, &dialog, poll);
	timer.start();
	QTimer::singleShot(0, &dialog, poll);
	dialog.exec();
}

bool DialogFactory::faceAuth(QWidget *parent, const std::string &faceUrl, FacePollRequest requestPoll,
			     FaceRetryRequest requestRetry, bool requiresVerification)
{
	constexpr int maxQrSize = 480;
	FaceDialog dialog(parent);
	configureDialog(dialog, text("Face.Title"));
	QLabel *qrLabel = new QLabel();
	const QPixmap pixmap = Core::QrGenerator::generate(faceUrl, maxQrSize);
	if (pixmap.isNull()) {
		qrLabel->setText(text("Face.GenerateFailed"));
	} else {
		qrLabel->setPixmap(pixmap);
	}
	qrLabel->setAlignment(Qt::AlignCenter);
	QLabel *tipLabel = new QLabel(text("Face.Instruction"));
	tipLabel->setWordWrap(true);
	tipLabel->setAlignment(Qt::AlignCenter);
	QLabel *statusLabel = new QLabel(text("Face.Waiting"));
	statusLabel->setObjectName("faceWaitingStatus");
	statusLabel->setWordWrap(true);
	statusLabel->setAlignment(Qt::AlignCenter);
	QPushButton *closeButton = new QPushButton(text("Face.Cancel"));
	dialogLayout(dialog)->addWidget(tipLabel);
	dialogLayout(dialog)->addWidget(qrLabel);
	dialogLayout(dialog)->addWidget(statusLabel);
	dialogLayout(dialog)->addWidget(closeButton);

	auto *retryButton = new QPushButton(text("Face.ManualRetry"));
	retryButton->setEnabled(!requiresVerification);
	dialogLayout(dialog)->insertWidget(3, retryButton);
	auto *detail = new QLabel(requiresVerification ? QString{} : text("Face.WaitingHint"));
	detail->setObjectName("faceWaitingDetail");
	detail->setWordWrap(true);
	detail->setTextFormat(Qt::PlainText);
	dialogLayout(dialog)->insertWidget(3, detail);
	struct PollState {
		bool inFlight = false;
		bool manualRetryOnly = false;
		bool verified = false;
		bool expired = false;
		Core::FaceAuthRetryPolicy retryPolicy;
		QElapsedTimer elapsed;
	};
	auto state = std::make_shared<PollState>();
	state->elapsed.start();
	QTimer timer(&dialog);
	timer.setInterval(Core::FaceAuthRetryPolicy::pollIntervalMs);
	QPointer<QDialog> guard(&dialog);
	const auto showExpired = [state, &timer, qrLabel, statusLabel, detail, retryButton, requiresVerification]() {
		state->expired = true;
		timer.stop();
		if (requiresVerification)
			qrLabel->clear();
		statusLabel->setText(text(requiresVerification ? "Face.ChallengeExpired" : "Face.Timeout"));
		detail->clear();
		retryButton->setEnabled(false);
	};
	const auto expire = [state, showExpired]() {
		if (!state->expired && (state->verified || !state->retryPolicy.expired(state->elapsed.elapsed())))
			return false;
		showExpired();
		return true;
	};
	const auto retry = [guard, state, &timer, qrLabel, statusLabel, detail, retryButton, requestRetry,
			    expire, requiresVerification]() {
		if (!guard || state->inFlight || (requiresVerification && !state->verified))
			return;
		if (expire())
			return;
		state->inFlight = true;
		if (requiresVerification) {
			statusLabel->setText(text("Face.Retrying"));
			detail->clear();
		}
		retryButton->setEnabled(false);
		guard->setProperty("startPending", true);
		requestRetry([guard, state, &timer, qrLabel, statusLabel, detail,
			      retryButton, expire](Core::FaceAuthRetryDecision decision, std::string retryFaceUrl,
					   std::string retryMessage) {
			if (!guard)
				return;
			state->inFlight = false;
			guard->setProperty("startPending", false);
			retryButton->setEnabled(true);
			if (decision == Core::FaceAuthRetryDecision::ContinueWaiting) {
				if (expire())
					return;
				state->manualRetryOnly = false;
				statusLabel->setText(text("Face.Waiting"));
				detail->setText(text("Face.WaitingHint"));
				state->retryPolicy.deferRetry(state->elapsed.elapsed());
				if (!retryFaceUrl.empty()) {
					const QPixmap retryPixmap = Core::QrGenerator::generate(retryFaceUrl, maxQrSize);
					if (!retryPixmap.isNull())
						qrLabel->setPixmap(retryPixmap);
				}
				// Keep waiting guidance stable while the same challenge remains active.
				return;
			}
			if (decision == Core::FaceAuthRetryDecision::VersionTimedOut) {
				if (expire())
					return;
				state->manualRetryOnly = true;
				statusLabel->setText(text("Face.RetryAvailable"));
				detail->setText(QString::fromUtf8(retryMessage));
				return;
			}
			timer.stop();
			if (decision == Core::FaceAuthRetryDecision::Started ||
			    decision == Core::FaceAuthRetryDecision::ChallengeChanged ||
			    decision == Core::FaceAuthRetryDecision::LoginRequired)
				guard->accept();
			else {
				statusLabel->setText(text("Dialog.StartFailed"));
				detail->setText(QString::fromStdString(retryMessage));
				retryButton->setEnabled(false);
			}
		});
	};
	const auto poll = [guard, state, &timer, detail, requestPoll, retry, expire, showExpired, requiresVerification]() {
		if (!guard || state->inFlight)
			return;
		if (expire() || state->manualRetryOnly) {
			return;
		}
		state->inFlight = true;
		requestPoll([guard, state, &timer, detail, retry, expire, showExpired,
			     requiresVerification](Bili::FaceAuthStatus status) {
			if (!guard)
				return;
			state->inFlight = false;
			using Status = Bili::FaceAuthStatus;
			if (status == Status::LoginRequired) {
				timer.stop();
				guard->accept();
				return;
			}
			if (expire()) {
				return;
			}
			if (status == Status::Expired) {
				showExpired();
				return;
			}
			if (requiresVerification) {
				detail->setText(status == Status::Unavailable ? text("Face.StatusUnavailable") : QString{});
				if (status == Status::Verified) {
					state->verified = true;
					timer.stop();
				}
			}
			if (state->retryPolicy.shouldRetry(status != Status::Unavailable, status == Status::Verified,
							   state->elapsed.elapsed()))
				retry();
		});
	};
	QObject::connect(retryButton, &QPushButton::clicked, &dialog, retry);
	QObject::connect(&timer, &QTimer::timeout, &dialog, poll);
	QObject::connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::reject);
	timer.start();
	QTimer::singleShot(0, &dialog, poll);
	return dialog.exec() == QDialog::Accepted;
}

void DialogFactory::roomSettings(QWidget *parent, const std::string &roomUrl, const std::string &currentTitle,
				 int currentAreaId, int currentPartId, const json11::Json &partitionData,
				 RoomApplyRequest onApply)
{
	RoomDialog dialog(parent);
	configureDialog(dialog, text("Room.DialogTitle"));
	dialogLayout(dialog)->addWidget(new QLabel(text("Room.Url").arg(QString::fromStdString(roomUrl))));
	QLineEdit *titleInput = new QLineEdit(QString::fromStdString(currentTitle));
	titleInput->setObjectName("roomTitle");
	QHBoxLayout *titleRow = new QHBoxLayout();
	titleRow->addWidget(new QLabel(text("Room.TitleLabel")));
	titleRow->addWidget(titleInput);
	dialogLayout(dialog)->addLayout(titleRow);

	QComboBox *partCombo = new QComboBox();
	QComboBox *areaCombo = new QComboBox();
	partCombo->setObjectName("roomParentArea");
	areaCombo->setObjectName("roomArea");
	QHBoxLayout *partitionRow = new QHBoxLayout();
	partitionRow->addWidget(new QLabel(text("Room.AreaLabel")));
	partitionRow->addWidget(partCombo);
	partitionRow->addWidget(areaCombo);
	dialogLayout(dialog)->addLayout(partitionRow);
	QPushButton *confirmChanges = new QPushButton(text("Room.Apply"));
	confirmChanges->setObjectName("applyRoomChanges");
	dialogLayout(dialog)->addWidget(confirmChanges);
	auto *feedback = new QLabel;
	feedback->setObjectName("roomApplyFeedback");
	feedback->setWordWrap(true);
	feedback->setTextFormat(Qt::PlainText);
	feedback->setTextInteractionFlags(Qt::TextSelectableByMouse);
	dialogLayout(dialog)->addWidget(feedback);
	auto *close = new QPushButton(text("Common.Close"));
	close->setObjectName("closeRoomSettings");
	dialogLayout(dialog)->addWidget(close);
	QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::reject);

	struct Part {
		int id;
		std::string name;
		std::vector<json11::Json> areas;
	};
	std::vector<Part> parts;
	if (partitionData.is_array()) {
		for (const auto &item : partitionData.array_items()) {
			int partId = 0;
			if (!Core::jsonInteger(item["id"], partId, 1) || !item["list"].is_array())
				continue;
			parts.push_back({partId, item["name"].string_value(), item["list"].array_items()});
		}
	}

	size_t selectedPartIndex = 0;
	for (size_t index = 0; index < parts.size(); ++index) {
		partCombo->addItem(QString::fromStdString(parts[index].name), parts[index].id);
		if (parts[index].id == currentPartId)
			selectedPartIndex = index;
	}
	if (!parts.empty()) {
		partCombo->setCurrentIndex(static_cast<int>(selectedPartIndex));
	} else if (currentPartId > 0) {
		partCombo->addItem(text("Room.CurrentParentArea"), currentPartId);
	} else {
		partCombo->addItem(text("Room.MissingParentArea"), 0);
	}

	const auto updateAreaCombo = [areaCombo, parts, currentAreaId](int partIndex) {
		areaCombo->clear();
		if (partIndex >= 0 && static_cast<size_t>(partIndex) < parts.size()) {
			int selectedAreaIndex = 0;
			for (size_t index = 0; index < parts[partIndex].areas.size(); ++index) {
				const auto &area = parts[partIndex].areas[index];
				int areaId = 0;
				if (!Core::jsonInteger(area["id"], areaId, 1))
					continue;
				areaCombo->addItem(QString::fromStdString(area["name"].string_value()), areaId);
				if (areaId == currentAreaId)
					selectedAreaIndex = areaCombo->count() - 1;
			}
			if (areaCombo->count() > 0)
				areaCombo->setCurrentIndex(selectedAreaIndex);
		} else if (currentAreaId > 0) {
			areaCombo->addItem(text("Room.CurrentArea"), currentAreaId);
		} else {
			areaCombo->addItem(text("Room.MissingArea"), 0);
		}
	};
	updateAreaCombo(static_cast<int>(selectedPartIndex));
	QObject::connect(partCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog, updateAreaCombo);
	struct ApplyState {
		std::string title;
		int areaId, partId;
		bool pending = false;
	};
	auto state = std::make_shared<ApplyState>(ApplyState{currentTitle, currentAreaId, currentPartId, false});
	const QPointer<RoomDialog> guard(&dialog);
	auto setPending = [=](bool pending) {
		state->pending = pending;
		if (!guard)
			return;
		guard->setProperty("operationPending", pending);
		QWidget *widgets[]{titleInput, partCombo, areaCombo, confirmChanges, close};
		for (auto *widget : widgets)
			widget->setEnabled(!pending);
		confirmChanges->setText(text(pending ? "Room.Applying" : "Room.Apply"));
	};
	QObject::connect(confirmChanges, &QPushButton::clicked, &dialog, [=]() {
		if (state->pending || !onApply)
			return;
		RoomChanges changes{state->title,
				    titleInput->text().trimmed().toUtf8().constData(),
				    state->areaId,
				    state->partId,
				    areaCombo->currentData().toInt(),
				    partCombo->currentData().toInt()};
		if ((changes.title.empty() || changes.title == changes.baseTitle) &&
		    (changes.areaId <= 0 ||
		     (changes.areaId == changes.baseAreaId && changes.partId == changes.basePartId))) {
			feedback->setText(text("Room.NoChanges"));
			return;
		}
		setPending(true);
		feedback->setText(text("Room.Applying"));
		onApply(changes, [=](RoomApplyResult result) {
			if (!guard || !state->pending)
				return;
			setPending(false);
			QStringList messages;
			if (result.titleUpdated) {
				state->title = changes.title;
				messages.append(text("Room.TitleUpdated"));
			}
			if (result.areaUpdated) {
				state->areaId = changes.areaId;
				state->partId = changes.partId;
				messages.append(text("Room.AreaUpdated"));
			}
			if (!result.message.empty())
				messages.append(QString::fromUtf8(result.message));
			if (messages.isEmpty())
				messages.append(text("Room.ApplyFailed"));
			feedback->setText(messages.join(QStringLiteral("\n")));
		});
	});
	dialog.exec();
}
} // namespace UI
