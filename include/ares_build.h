#pragma once

#if defined(_WIN32)
#include "ares_build_windows.h"
#endif

#if defined(__linux__)
#include "ares_build_linux.h"
#endif

#if defined(__APPLE__)
#include "ares_build_macos.h"
#endif