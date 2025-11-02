#pragma once

// Windows errno compatibility definitions
// These constants are not defined in Windows headers by default
// This header must be included before any Windows headers

// Only define errno constants if they're not already defined by the system
#ifndef EINVAL
#define EINVAL 22
#endif

#ifndef ERANGE
#define ERANGE 34
#endif

#ifndef ETIMEDOUT
#define ETIMEDOUT 110
#endif

#ifndef EINTR
#define EINTR 4
#endif

#ifndef EBADF
#define EBADF 9
#endif

#ifndef EAGAIN
#define EAGAIN 11
#endif

#ifndef EWOULDBLOCK
#define EWOULDBLOCK 11
#endif

#ifndef EPIPE
#define EPIPE 32
#endif

#ifndef ECONNREFUSED
#define ECONNREFUSED 111
#endif

#ifndef ENETUNREACH
#define ENETUNREACH 101
#endif

#ifndef EHOSTUNREACH
#define EHOSTUNREACH 113
#endif

#ifndef ECONNRESET
#define ECONNRESET 104
#endif

#ifndef ENOTSOCK
#define ENOTSOCK 88
#endif

// Declare errno variable for Windows CRT compatibility
extern int errno;
