#pragma once

/* libplatform exposes its debug counters through C11 _Atomic syntax.  MSVC
 * accepts the C spelling only in C mode; map it to the equivalent C++ type
 * when a shared C header is parsed as C++. */
#ifdef __cplusplus
#include <atomic>
#ifndef _Atomic
#define _Atomic(type) std::atomic<type>
#endif
#ifndef _Static_assert
#define _Static_assert static_assert
#endif
#else
/* MSVC's C headers do not expose C11 max_align_t in C mode. */
typedef double max_align_t;
#endif
