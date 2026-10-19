#pragma once
#include "proxy/proxy_runtime.hpp"
#include <QWidget>
#include <functional>
namespace UI {
void proxyDialog(QWidget *parent, const Proxy::Settings &current, Proxy::Runtime &runtime, const QString &corePath,
		 std::function<bool()> locked, std::function<bool(const Proxy::Settings &, QString &)> apply);
}
