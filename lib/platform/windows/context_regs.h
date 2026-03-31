/**
 * @file platform/windows/context_regs.h
 * @brief Architecture-aware CONTEXT register access macros
 *
 * ARM64 Windows uses Pc/Sp/Fp instead of x64's Rip/Rsp/Rbp.
 */

#pragma once

#include <windows.h>
#include <dbghelp.h>

#if defined(_M_ARM64) || defined(__aarch64__)
#define CTX_PC(ctx)    ((ctx)->Pc)
#define CTX_SP(ctx)    ((ctx)->Sp)
#define CTX_FP(ctx)    ((ctx)->Fp)
#define CTX_PC_NAME    "PC"
#define IMAGE_FILE_MACHINE_CURRENT IMAGE_FILE_MACHINE_ARM64
#else
#define CTX_PC(ctx)    ((ctx)->Rip)
#define CTX_SP(ctx)    ((ctx)->Rsp)
#define CTX_FP(ctx)    ((ctx)->Rbp)
#define CTX_PC_NAME    "RIP"
#define IMAGE_FILE_MACHINE_CURRENT IMAGE_FILE_MACHINE_AMD64
#endif
