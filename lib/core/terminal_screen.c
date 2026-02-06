// =============================================================================
// Terminal Screen Rendering Abstraction
// =============================================================================
// Provides a reusable "fixed header + scrolling logs" pattern for terminal UIs.
// This module extracts the common rendering logic from splash.c and server_status.c
// into a unified, testable abstraction.
//
// Design:
// - Callback-based architecture for header content generation
// - Automatic log scrolling with line wrapping
// - Thread-safe log buffer management
// - Terminal resize handling
// - Color scheme integration
//
// Usage:
// 1. Create a terminal_screen_t instance
// 2. Register a header callback function
// 3. Call terminal_screen_render() in your main loop
// 4. Use terminal_screen_log() to add log lines
// =============================================================================

#include "ascii-chat/core/terminal_screen.h"

// Stub implementation - will be implemented in subsequent tasks
