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
#include <QTimer>

namespace UI {
void streamRoutesDialog(QWidget *parent, const Core::StreamRoutes &routes, int selected, const std::string &key,
			bool streamProxy, bool currentSession, std::function<bool(const QString &, QString &)> apply,
			StreamRouteActions actions)
{
	QDialog dialog(parent);
	dialog.setWindowTitle(currentSession ? QStringLiteral("B 站开播成功 · 推流地址与线路")
					     : QStringLiteral("推流地址与线路"));
	dialog.resize(700, 420);
	auto *layout = new QVBoxLayout(&dialog);
	auto *liveStatus = new QLabel;
	liveStatus->setObjectName("streamSessionStatus");
	liveStatus->setWordWrap(true);
	QFont headingFont = liveStatus->font();
	headingFont.setBold(true);
	liveStatus->setFont(headingFont);
	layout->addWidget(liveStatus);
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
	status->setObjectName("streamRouteStatus");
	status->setTextFormat(Qt::PlainText);
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
	auto *hint = new QLabel(QStringLiteral("一键推流将使用当前所选线路启动 OBS。关闭窗口不会关闭 B 站直播。"));
	hint->setWordWrap(true);
	layout->addWidget(hint);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
	buttons->button(QDialogButtonBox::Close)->setText(QStringLiteral("关闭"));
	auto *start = buttons->addButton(QStringLiteral("一键推流"), QDialogButtonBox::ActionRole);
	start->setObjectName("startObsStream");
	for (auto *button : {copy, copyKey, replace, start, buttons->button(QDialogButtonBox::Close)})
		button->setAutoDefault(false);
	layout->addWidget(buttons);
	bool pending = false;
	const auto refresh = [&] {
		const bool active = actions.obsActive && actions.obsActive();
		const bool session = actions.sessionActive ? actions.sessionActive() : currentSession;
		dialog.setWindowTitle(session ? QStringLiteral("B 站开播成功 · 推流地址与线路")
					      : QStringLiteral("推流地址与线路"));
		liveStatus->setText(session ? (active ? QStringLiteral("B 站开播成功，OBS 正在推流。")
						      : QStringLiteral("B 站开播成功，OBS 尚未开始推流。"))
					    : QStringLiteral("历史推流线路：尚未确认当前 B 站直播状态。"));
		start->setEnabled(!pending && session && !active && !key.empty() && !routes.isEmpty() &&
				  bool(actions.startObs) && (!actions.canStartObs || actions.canStartObs()));
		replace->setEnabled(!pending && !active && !key.empty() && !routes.isEmpty());
	};
	QObject::connect(start, &QPushButton::clicked, &dialog, [&] {
		if (!start->isEnabled() || pending)
			return;
		pending = true;
		refresh();
		QString error;
		if (actions.startObs && actions.startObs(address->text(), error)) {
			dialog.accept();
			return;
		}
		pending = false;
		status->setText(error.isEmpty() ? QStringLiteral("OBS 推流未能启动，请检查当前直播状态。") : error);
		refresh();
	});
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
	QTimer stateTimer(&dialog);
	QObject::connect(&stateTimer, &QTimer::timeout, &dialog, refresh);
	stateTimer.start(250);
	refresh();
	dialog.exec();
}
} // namespace UI
