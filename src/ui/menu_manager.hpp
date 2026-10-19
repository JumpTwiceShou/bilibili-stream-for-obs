#pragma once
#include <QMenuBar>
#include <QAction>
#include <QMenu>
#include <QObject>
#include <QPointer>

namespace UI {
class MenuManager : public QObject {
	Q_OBJECT
public:
	struct Actions {
		QAction *scanQrcode = nullptr;
		QAction *loginStatus = nullptr;
		QAction *logout = nullptr;
		QAction *streamToggle = nullptr;
		QAction *openRoom = nullptr;
		QAction *updateRoomInfo = nullptr;
		QAction *proxySettings = nullptr;
		QAction *syncSettings = nullptr;
		QAction *streamRoutes = nullptr;
	};

	explicit MenuManager(QMenuBar *menuBar, QObject *parent = nullptr);
	~MenuManager() override;
	Actions &actions() { return m_actions; }

signals:
	void scanQrcodeClicked();
	void logoutClicked();
	void streamToggleClicked();
	void openRoomClicked();
	void updateRoomInfoClicked();
	void proxySettingsClicked();
	void syncSettingsClicked();
	void streamRoutesClicked();

private:
	void setupMenu();
	Actions m_actions;
	QPointer<QMenuBar> m_menuBar;
	QPointer<QMenu> m_rootMenu;
};
} // namespace UI
