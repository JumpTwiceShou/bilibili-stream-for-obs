#pragma once

#include <QString>
#include <obs-module.h>

namespace UI {
inline QString text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}
} // namespace UI
