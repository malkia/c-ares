#pragma once

#if defined(_WIN32)
#include "ares_config_windows.h"
#endif

#if defined(__linux__)
#include "ares_config_linux.h"
#endif

#if defined(__APPLE__)
#include "ares_config_macos.h"
#endif