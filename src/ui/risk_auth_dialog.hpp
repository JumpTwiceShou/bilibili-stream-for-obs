#pragma once
#include "proxy/proxy_runtime.hpp"
#include <QString>
class QWidget;

namespace UI {
// Runs Bilibili's current official challenge SDK. Success requires its server-validated token.
QString officialRiskAuth(QWidget *parent, const std::string &cookies, const std::string &voucher, bool proxyRequired,
			 const Proxy::Endpoint &endpoint, QString &error);
} // namespace UI
