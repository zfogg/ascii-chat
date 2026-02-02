#pragma once

/**
 * @file crypto/gpg/gpg.h
 * @brief GPG operations - main header
 * @ingroup crypto
 * @addtogroup crypto
 * @{
 *
 * This header provides the complete GPG interface by including all submodule headers.
 * Users can include this single header to access all GPG functionality.
 *
 * Submodules:
 * - agent.h: GPG agent connection and communication
 * - export.h: Public key export from GPG keyring
 * - signing.h: Message signing operations
 * - verification.h: Signature verification operations
 */

#include "agent.h"
#include "export.h"
#include "signing.h"
#include "verification.h"

/** @} */
