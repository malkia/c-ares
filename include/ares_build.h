#pragma once

#ifdef CARES_NO_DEPRECATED
#error CARES_NO_DEPRECATED can't be defined outside this header file
#undef CARES_NO_DEPRECATED
#endif

// malkia: grpc still calls ares_gethostbyname so we can't deprecate it yet
#define CARES_NO_DEPRECATED 1

#if defined(_WIN32)
#include "ares_build_windows.h"
#endif

#if defined(__linux__)
#include "ares_build_linux.h"
#endif

#if defined(__APPLE__)
#include "ares_build_macos.h"
#endif