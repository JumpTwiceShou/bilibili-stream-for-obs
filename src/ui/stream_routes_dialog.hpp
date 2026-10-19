#pragma once
#include "core/stream_routes.hpp"
#include <QWidget>
#include <functional>

namespace UI {
void streamRoutesDialog(QWidget *parent, const Core::StreamRoutes &routes, int selected, const std::string &key,
			bool streamProxy, bool currentSession, std::function<bool(const QString &, QString &)> apply);
} // namespace UI
