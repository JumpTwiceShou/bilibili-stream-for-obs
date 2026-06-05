#include "ui/dialog_factory.hpp"
#include <obs-frontend-api.h>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include "bilibili_api.hpp"
#include "core/qr_generator.hpp"

namespace UI {
namespace {
QDialog *createBaseDialog(const QString &title, QWidget *parent)
{
	QDialog *dialog = new QDialog(parent);
	dialog->setWindowTitle(title);
	QVBoxLayout *layout = new QVBoxLayout(dialog);
	dialog->setLayout(layout);
	return dialog;
}
} // namespace

void DialogFactory::message(const QString &msg, const QString &title, QWidget *parent)
{
	QDialog *dialog = createBaseDialog(title, parent ? parent : (QWidget *)obs_frontend_get_main_window());
	QVBoxLayout *layout = (QVBoxLayout *)dialog->layout();
	layout->addWidget(new QLabel(msg));
	QPushButton *confirm = new QPushButton(QStringLiteral("确认"));
	layout->addWidget(confirm);
	QObject::connect(confirm, &QPushButton::clicked, dialog, &QDialog::accept);
	QObject::connect(dialog, &QDialog::finished, dialog, &QDialog::deleteLater);
	dialog->exec();
}

QDialog *DialogFactory::qrLogin(QWidget *parent, const std::string &qrData, std::string &qrKey,
				std::function<void(const std::string &cookies)> onSuccess)
{
	QDialog *dialog = createBaseDialog(QStringLiteral("Bilibili 登录二维码"), parent);
	QVBoxLayout *layout = (QVBoxLayout *)dialog->layout();

	QLabel *qrLabel = new QLabel();
	QPixmap pixmap = Core::QrGenerator::generate(qrData);
	if (pixmap.isNull()) {
		qrLabel->setText(QStringLiteral("无法生成二维码"));
	} else {
		qrLabel->setPixmap(pixmap);
	}
	layout->addWidget(qrLabel);
	layout->addWidget(new QLabel(QStringLiteral("使用手机扫描二维码登录")));

	QTimer *timer = new QTimer(dialog);
	int retryCount = 0;

	auto timerCallback = [&, retryCount, qrKey, onSuccess, timer, qrLabel, dialog]() mutable {
		if (retryCount > 180) {
			timer->stop();
			qrLabel->setText(QStringLiteral("二维码已过期，请重新打开"));
			return;
		}
		++retryCount;

		std::string cookies, message;
		if (Bili::BiliApi::qrLogin(qrKey, cookies, message)) {
			timer->stop();
			if (onSuccess)
				onSuccess(cookies);
			dialog->accept();
		}
	};

	QObject::connect(timer, &QTimer::timeout, timerCallback);
	QObject::connect(dialog, &QDialog::finished, [timer, dialog]() {
		timer->stop();
		dialog->deleteLater();
	});

	timer->start(1000);
	dialog->exec();
	return dialog;
}

QDialog *DialogFactory::streamStarted(QWidget *parent, const std::string &rtmpAddr, const std::string &rtmpCode)
{
	QDialog *dialog = createBaseDialog(QStringLiteral("消息"), parent);
	QVBoxLayout *layout = (QVBoxLayout *)dialog->layout();

	layout->addWidget(new QLabel(
		QStringLiteral("Bilibili 已开始直播，请复制以下内容进行推流。\n"
			       "如果自定义推流失败，请尝试使用 B 站预设推流。\n"
			       "RTMP 地址: %1\n推流码: %2")
			.arg(QString::fromStdString(rtmpAddr), QString::fromStdString(rtmpCode))));

	QPushButton *copy = new QPushButton(QStringLiteral("复制"));
	QPushButton *confirm = new QPushButton(QStringLiteral("确认"));
	layout->addWidget(copy);
	layout->addWidget(confirm);

	QObject::connect(copy, &QPushButton::clicked, [=]() {
		QApplication::clipboard()->setText(
			QStringLiteral("推流地址: %1\n推流码: %2")
				.arg(QString::fromStdString(rtmpAddr), QString::fromStdString(rtmpCode)));
	});
	QObject::connect(confirm, &QPushButton::clicked, dialog, &QDialog::accept);
	QObject::connect(dialog, &QDialog::finished, dialog, &QDialog::deleteLater);

	dialog->exec();
	return dialog;
}

QDialog *DialogFactory::faceAuth(QWidget *parent, const std::string &faceUrl,
				 std::function<bool(std::string &statusMessage)> pollStatus,
				 std::function<void()> onVerified)
{
	QDialog *dialog = createBaseDialog(QStringLiteral("实名认证（人脸验证）"), parent);
	QVBoxLayout *layout = (QVBoxLayout *)dialog->layout();

	QLabel *qrLabel = new QLabel();
	QPixmap pixmap = Core::QrGenerator::generate(faceUrl);
	if (pixmap.isNull()) {
		qrLabel->setText(QStringLiteral("二维码生成失败，请检查网络或日志"));
	} else {
		qrLabel->setPixmap(pixmap);
	}
	qrLabel->setAlignment(Qt::AlignCenter);

	QLabel *tipLabel = new QLabel(QStringLiteral(
		"请使用 <b>手机 Bilibili App</b> 扫描下方二维码完成人脸验证。<br>"
		"验证完成后，插件会自动重试开播。"));
	tipLabel->setWordWrap(true);
	tipLabel->setAlignment(Qt::AlignCenter);

	QLabel *statusLabel = new QLabel(QStringLiteral("正在等待人脸验证完成..."));
	statusLabel->setWordWrap(true);
	statusLabel->setAlignment(Qt::AlignCenter);

	QPushButton *closeBtn = new QPushButton(QStringLiteral("取消等待"));

	layout->addWidget(tipLabel);
	layout->addWidget(qrLabel);
	layout->addWidget(statusLabel);
	layout->addWidget(closeBtn);

	QTimer *timer = nullptr;
	if (pollStatus) {
		timer = new QTimer(dialog);
		int retryCount = 0;
		QObject::connect(timer, &QTimer::timeout, [=, &retryCount]() mutable {
			if (++retryCount > 120) {
				timer->stop();
				statusLabel->setText(QStringLiteral("等待人脸验证超时，请关闭窗口后重试。"));
				return;
			}

			std::string statusMessage;
			if (pollStatus(statusMessage)) {
				timer->stop();
				statusLabel->setText(QStringLiteral("人脸验证已通过，正在自动开播..."));
				dialog->accept();
				if (onVerified) {
					onVerified();
				}
				return;
			}

			if (!statusMessage.empty()) {
				statusLabel->setText(QString::fromUtf8(statusMessage));
			}
		});
		timer->start(5000);
	}

	QObject::connect(closeBtn, &QPushButton::clicked, dialog, &QDialog::accept);
	QObject::connect(dialog, &QDialog::finished, [dialog, timer]() {
		if (timer) {
			timer->stop();
		}
		dialog->deleteLater();
	});

	dialog->exec();
	return dialog;
}

QDialog *DialogFactory::roomSettings(QWidget *parent, const std::string &roomUrl, const std::string &currentTitle,
				     int currentAreaId, int currentPartId,
				     std::function<void(const std::string &title, int areaId, int partId)> onApply)
{
	QDialog *dialog = createBaseDialog(QStringLiteral("更新直播间信息"), parent);
	QVBoxLayout *layout = (QVBoxLayout *)dialog->layout();

	layout->addWidget(
		new QLabel(QStringLiteral("直播间：https://live.bilibili.com/%1").arg(QString::fromStdString(roomUrl))));

	QLineEdit *titleInput = new QLineEdit(QString::fromStdString(currentTitle));
	QHBoxLayout *titleRow = new QHBoxLayout();
	titleRow->addWidget(new QLabel(QStringLiteral("直播间标题")));
	titleRow->addWidget(titleInput);
	layout->addLayout(titleRow);

	QComboBox *partCombo = new QComboBox();
	QComboBox *areaCombo = new QComboBox();
	QHBoxLayout *partitionRow = new QHBoxLayout();
	partitionRow->addWidget(new QLabel(QStringLiteral("直播间分区")));
	partitionRow->addWidget(partCombo);
	partitionRow->addWidget(areaCombo);
	layout->addLayout(partitionRow);

	QPushButton *confirmChanges = new QPushButton(QStringLiteral("确认修改"));
	layout->addWidget(confirmChanges);

	struct Part {
		int id;
		std::string name;
		std::vector<json11::Json> list;
	};
	std::vector<Part> parts;
	std::string message;
	auto partitionData = Bili::BiliApi::getPartitionList(message);
	if (partitionData.is_array()) {
		for (const auto &item : partitionData.array_items()) {
			parts.push_back({item["id"].int_value(), item["name"].string_value(), item["list"].array_items()});
		}
	}

	size_t selectedPartIndex = 0;
	bool hasCurrentPart = false;
	for (size_t i = 0; i < parts.size(); ++i) {
		partCombo->addItem(QString::fromStdString(parts[i].name), parts[i].id);
		if (parts[i].id == currentPartId) {
			selectedPartIndex = i;
			hasCurrentPart = true;
		}
	}
	if (!parts.empty()) {
		partCombo->setCurrentIndex(static_cast<int>(selectedPartIndex));
	} else if (currentPartId > 0) {
		partCombo->addItem(QStringLiteral("当前主分区"), currentPartId);
	} else {
		partCombo->addItem(QStringLiteral("未读取到主分区"), 0);
	}

	auto updateAreaCombo = [=](int partIndex) {
		areaCombo->clear();
		if (partIndex >= 0 && static_cast<size_t>(partIndex) < parts.size() && !parts[partIndex].list.empty()) {
			size_t selectedAreaIndex = 0;
			bool hasCurrentArea = false;
			for (size_t i = 0; i < parts[partIndex].list.size(); ++i) {
				int id = std::stoi(parts[partIndex].list[i]["id"].string_value());
				QString name = QString::fromStdString(parts[partIndex].list[i]["name"].string_value());
				areaCombo->addItem(name, id);
				if (id == currentAreaId) {
					selectedAreaIndex = i;
					hasCurrentArea = true;
				}
			}
			if (hasCurrentArea || hasCurrentPart) {
				areaCombo->setCurrentIndex(static_cast<int>(selectedAreaIndex));
			}
		} else if (currentAreaId > 0) {
			areaCombo->addItem(QStringLiteral("当前分区"), currentAreaId);
		} else {
			areaCombo->addItem(QStringLiteral("未读取到分区"), 0);
		}
	};
	updateAreaCombo(static_cast<int>(selectedPartIndex));
	QObject::connect(partCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), updateAreaCombo);

	QObject::connect(confirmChanges, &QPushButton::clicked, [=]() {
		if (onApply) {
			onApply(titleInput->text().trimmed().toUtf8().constData(), areaCombo->currentData().toInt(),
				partCombo->currentData().toInt());
		}
		dialog->accept();
	});

	QObject::connect(dialog, &QDialog::finished, dialog, &QDialog::deleteLater);
	dialog->exec();
	return dialog;
}

} // namespace UI
