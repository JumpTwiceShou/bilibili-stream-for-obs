#include "ui/dialog_factory.hpp"

#include <QApplication>
#include <QClipboard>
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

bool DialogFactory::confirm(const QString &msg, const QString &title, QWidget *parent)
{
	QDialog dialog(defaultParent(parent));
	configureDialog(dialog, title);
	QLabel *label = new QLabel(msg);
	label->setTextFormat(Qt::PlainText);
	label->setWordWrap(true);
	dialogLayout(dialog)->addWidget(label);
	QHBoxLayout *buttons = new QHBoxLayout();
	QPushButton *cancel = new QPushButton(text("Common.Cancel"));
	QPushButton *confirm = new QPushButton(text("Common.Confirm"));
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

void DialogFactory::streamStarted(QWidget *parent, const std::string &rtmpAddr, const std::string &rtmpCode)
{
	QDialog dialog(parent);
	configureDialog(dialog, text("Common.Message"));
	QLabel *credentials = new QLabel(
		text("Stream.Credentials").arg(QString::fromStdString(rtmpAddr), QString::fromStdString(rtmpCode)));
	credentials->setTextFormat(Qt::PlainText);
	credentials->setTextInteractionFlags(Qt::TextSelectableByMouse);
	credentials->setWordWrap(true);
	dialogLayout(dialog)->addWidget(credentials);

	QPushButton *copy = new QPushButton(text("Common.Copy"));
	QPushButton *confirm = new QPushButton(text("Common.Confirm"));
	dialogLayout(dialog)->addWidget(copy);
	dialogLayout(dialog)->addWidget(confirm);
	QObject::connect(copy, &QPushButton::clicked, &dialog, [rtmpAddr, rtmpCode]() {
		QApplication::clipboard()->setText(
			text("Stream.Clipboard")
				.arg(QString::fromStdString(rtmpAddr), QString::fromStdString(rtmpCode)));
	});
	QObject::connect(confirm, &QPushButton::clicked, &dialog, &QDialog::accept);
	dialog.exec();
}

void DialogFactory::faceAuth(QWidget *parent, const std::string &faceUrl, FacePollRequest requestPoll,
			     std::function<void()> onVerified)
{
	QDialog dialog(parent);
	configureDialog(dialog, text("Face.Title"));
	QLabel *qrLabel = new QLabel();
	const QPixmap pixmap = Core::QrGenerator::generate(faceUrl);
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
	statusLabel->setWordWrap(true);
	statusLabel->setAlignment(Qt::AlignCenter);
	QPushButton *closeButton = new QPushButton(text("Face.Cancel"));
	dialogLayout(dialog)->addWidget(tipLabel);
	dialogLayout(dialog)->addWidget(qrLabel);
	dialogLayout(dialog)->addWidget(statusLabel);
	dialogLayout(dialog)->addWidget(closeButton);

	struct PollState {
		bool inFlight = false;
		QElapsedTimer elapsed;
	};
	auto state = std::make_shared<PollState>();
	state->elapsed.start();
	QTimer timer(&dialog);
	timer.setInterval(5000);
	QPointer<QDialog> guard(&dialog);
	const auto poll = [guard, state, &timer, statusLabel, requestPoll, onVerified]() {
		if (!guard || state->inFlight)
			return;
		if (state->elapsed.elapsed() > 600000) {
			timer.stop();
			statusLabel->setText(text("Face.Timeout"));
			return;
		}
		state->inFlight = true;
		requestPoll([guard, state, &timer, statusLabel, onVerified](bool requestSucceeded, bool identified,
									    std::string statusMessage) {
			if (!guard)
				return;
			state->inFlight = false;
			if (!statusMessage.empty())
				statusLabel->setText(QString::fromUtf8(statusMessage));
			if (requestSucceeded && identified) {
				timer.stop();
				guard->accept();
				if (onVerified)
					onVerified();
			}
		});
	};
	QObject::connect(&timer, &QTimer::timeout, &dialog, poll);
	QObject::connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::reject);
	timer.start();
	QTimer::singleShot(0, &dialog, poll);
	dialog.exec();
}

void DialogFactory::roomSettings(QWidget *parent, const std::string &roomUrl, const std::string &currentTitle,
				 int currentAreaId, int currentPartId, const json11::Json &partitionData,
				 std::function<void(const std::string &title, int areaId, int partId)> onApply)
{
	QDialog dialog(parent);
	configureDialog(dialog, text("Room.DialogTitle"));
	dialogLayout(dialog)->addWidget(new QLabel(text("Room.Url").arg(QString::fromStdString(roomUrl))));
	QLineEdit *titleInput = new QLineEdit(QString::fromStdString(currentTitle));
	QHBoxLayout *titleRow = new QHBoxLayout();
	titleRow->addWidget(new QLabel(text("Room.TitleLabel")));
	titleRow->addWidget(titleInput);
	dialogLayout(dialog)->addLayout(titleRow);

	QComboBox *partCombo = new QComboBox();
	QComboBox *areaCombo = new QComboBox();
	QHBoxLayout *partitionRow = new QHBoxLayout();
	partitionRow->addWidget(new QLabel(text("Room.AreaLabel")));
	partitionRow->addWidget(partCombo);
	partitionRow->addWidget(areaCombo);
	dialogLayout(dialog)->addLayout(partitionRow);
	QPushButton *confirmChanges = new QPushButton(text("Room.Apply"));
	dialogLayout(dialog)->addWidget(confirmChanges);

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
	QObject::connect(confirmChanges, &QPushButton::clicked, &dialog, [=, &dialog]() {
		if (onApply) {
			onApply(titleInput->text().trimmed().toUtf8().constData(), areaCombo->currentData().toInt(),
				partCombo->currentData().toInt());
		}
		dialog.accept();
	});
	dialog.exec();
}
} // namespace UI
