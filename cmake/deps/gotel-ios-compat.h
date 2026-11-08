#pragma once

#if defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#include <Security/Security.h>
#define SecTrustCopyAnchorCertificates(roots) (errSecParam)
#endif
#endif
