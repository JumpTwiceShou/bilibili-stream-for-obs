#pragma once
#include "core/stream_routes.hpp"
#include <QWidget>
#include <functional>

namespace UI {
struct StreamRouteActions {
	std::function<bool()> obsActive;
	std::function<bool()> sessionActive;
	std::function<bool()> canStartObs;
	std::function<bool(const QString &, QString &)> startObs;
};
void streamRoutesDialog(QWidget *parent, const Core::StreamRoutes &routes, int selected, const std::string &key,
			bool streamProxy, bool currentSession, std::function<bool(const QString &, QString &)> apply,
			StreamRouteActions actions = {});
} // namespace UI
