#include "stream_routes_dialog.hpp"
#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace UI {
void streamRoutesDialog(QWidget *parent, const Core::StreamRoutes &routes, int selected, const std::string &key,
			bool streamProxy, bool currentSession, std::function<bool(const QString &, QString &)> apply)
{
	QDialog dialog(parent);
	dialog.setWindowTitle(QStringLiteral("推流地址与线路"));
	dialog.resize(700, 420);
	auto *layout = new QVBoxLayout(&dialog);
	auto *note = new QLabel(
		streamProxy
			? QStringLiteral("OBS 推流走代理：默认选择本次经代理开播后 B 站返回的地址。")
			: QStringLiteral(
				  "OBS 推流直连：默认选择实测连接延迟最低的可用线路。延迟仅用于推荐，不代表地理距离或持续推流带宽。"));
	note->setWordWrap(true);
	layout->addWidget(note);
	auto *list = new QListWidget;
	list->setObjectName("streamRouteList");
	for (const auto &route : routes) {
		const QString latency = route.latencyMs >= 0 ? QStringLiteral(" · 直连 %1 ms").arg(route.latencyMs)
							     : QStringLiteral(" · 未测得延迟");
		auto *item = new QListWidgetItem(route.name + latency + "\n" + route.url, list);
		item->setData(Qt::UserRole, route.url);
	}
	layout->addWidget(list);
	auto *address = new QLineEdit;
	address->setReadOnly(true);
	address->setObjectName("streamRouteAddress");
	layout->addWidget(address);
	auto *status = new QLabel;
	status->setWordWrap(true);
	status->setText(
		currentSession
			? QStringLiteral("点击线路即可复制地址；一键替换会将所选地址与推流码应用到 OBS。")
			: QStringLiteral(
				  "显示上次保存的线路；推流码可能已过期，可重新开播获取。点击线路即可复制地址。"));
	layout->addWidget(status);
	auto *copy = new QPushButton(QStringLiteral("复制推流地址"));
	auto *copyKey = new QPushButton(QStringLiteral("复制推流码"));
	auto *replace = new QPushButton(QStringLiteral("一键替换当前 OBS 推流配置"));
	replace->setObjectName("applyStreamRoute");
	copyKey->setEnabled(!key.empty());
	replace->setEnabled(!key.empty() && !routes.isEmpty());
	layout->addWidget(copy);
	layout->addWidget(copyKey);
	layout->addWidget(replace);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
	layout->addWidget(buttons);
	QObject::connect(list, &QListWidget::currentItemChanged, &dialog, [address](QListWidgetItem *item) {
		address->setText(item ? item->data(Qt::UserRole).toString() : QString());
	});
	QObject::connect(list, &QListWidget::itemClicked, &dialog, [status](QListWidgetItem *item) {
		QApplication::clipboard()->setText(item->data(Qt::UserRole).toString());
		status->setText(QStringLiteral("推流地址已复制。"));
	});
	QObject::connect(copy, &QPushButton::clicked, &dialog, [=] {
		QApplication::clipboard()->setText(address->text());
		status->setText(QStringLiteral("推流地址已复制。"));
	});
	QObject::connect(copyKey, &QPushButton::clicked, &dialog, [=] {
		QApplication::clipboard()->setText(QString::fromStdString(key));
		status->setText(QStringLiteral("推流码已复制。"));
	});
	QObject::connect(replace, &QPushButton::clicked, &dialog, [=] {
		QString error;
		status->setText(apply(address->text(), error) ? QStringLiteral("已替换当前 OBS 推流配置。") : error);
	});
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	list->setCurrentRow(selected >= 0 ? selected : 0);
	dialog.exec();
}
} // namespace UI
