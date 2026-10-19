#include "ui/menu_manager.hpp"
#include <QMenu>

#include "ui/locale.hpp"

namespace UI {
MenuManager::MenuManager(QMenuBar *menuBar, QObject *parent) : QObject(parent), m_menuBar(menuBar)
{
	setupMenu();
}

MenuManager::~MenuManager()
{
	if (m_menuBar && m_rootMenu) {
		m_menuBar->removeAction(m_rootMenu->menuAction());
		delete m_rootMenu;
	}
}

void MenuManager::setupMenu()
{
	QMenu *bilibiliMenu = m_menuBar->addMenu(text("Title"));
	m_rootMenu = bilibiliMenu;
	QMenu *loginMenu = bilibiliMenu->addMenu(text("Menu.Login"));

	m_actions.scanQrcode = loginMenu->addAction(text("Menu.ScanQr"));
	m_actions.loginStatus = loginMenu->addAction(text("Menu.LoginStatus.LoggedOut"));
	m_actions.loginStatus->setCheckable(true);
	m_actions.loginStatus->setEnabled(false);
	loginMenu->addSeparator();
	m_actions.logout = loginMenu->addAction(text("Menu.Logout"));

	m_actions.streamToggle = bilibiliMenu->addAction(text("Menu.StartLive"));
	m_actions.openRoom = bilibiliMenu->addAction(text("Menu.OpenRoom"));
	m_actions.updateRoomInfo = bilibiliMenu->addAction(text("Menu.UpdateRoom"));
	bilibiliMenu->addSeparator();
	m_actions.proxySettings = bilibiliMenu->addAction(text("Menu.ProxySettings"));
	m_actions.syncSettings = bilibiliMenu->addAction(text("Menu.SyncSettings"));
	m_actions.streamRoutes = bilibiliMenu->addAction(text("Menu.StreamRoutes"));

	connect(m_actions.scanQrcode, &QAction::triggered, this, &MenuManager::scanQrcodeClicked);
	connect(m_actions.logout, &QAction::triggered, this, &MenuManager::logoutClicked);
	connect(m_actions.streamToggle, &QAction::triggered, this, &MenuManager::streamToggleClicked);
	connect(m_actions.openRoom, &QAction::triggered, this, &MenuManager::openRoomClicked);
	connect(m_actions.updateRoomInfo, &QAction::triggered, this, &MenuManager::updateRoomInfoClicked);
	connect(m_actions.proxySettings, &QAction::triggered, this, &MenuManager::proxySettingsClicked);
	connect(m_actions.syncSettings, &QAction::triggered, this, &MenuManager::syncSettingsClicked);
	connect(m_actions.streamRoutes, &QAction::triggered, this, &MenuManager::streamRoutesClicked);
}
} // namespace UI
