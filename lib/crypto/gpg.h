#pragma once

/**
 * @file crypto/gpg.h
 * @brief GPG operations - main header
 * @ingroup crypto
 * @addtogroup crypto
 * @{
 *
 * This header provides the complete GPG interface by including all submodule headers.
 * Users can include this single header to access all GPG functionality.
 *
 * Submodules:
 * - gpg/agent.h: GPG agent connection and communication
 * - gpg/export.h: Public key export from GPG keyring
 * - gpg/signing.h: Message signing operations
 * - gpg/verification.h: Signature verification operations
 */

#include "gpg/agent.h"
#include "gpg/export.h"
#include "gpg/signing.h"
#include "gpg/verification.h"

/** @} */
