#include "defer.h"
#include <string.h>
#include <stdlib.h>
#include "common.h"
#include "logging.h"

void ascii_defer_scope_init(ascii_defer_scope_t *scope) {
    if (!scope) {
        return;
    }

    memset(scope, 0, sizeof(ascii_defer_scope_t));
    scope->executed = false;
}

bool ascii_defer_push(ascii_defer_scope_t *scope, ascii_defer_fn_t fn,
                       const void *context, size_t context_size) {
    if (!scope || !fn) {
        log_error("ascii_defer_push: Invalid arguments (scope=%p, fn=%p)",
                  (void*)scope, (void*)fn);
        return false;
    }

    if (scope->executed) {
        log_error("ascii_defer_push: Scope has already been executed");
        return false;
    }

    if (scope->count >= ASCII_DEFER_MAX_ACTIONS) {
        log_error("ascii_defer_push: Scope is full (max %d actions)",
                  ASCII_DEFER_MAX_ACTIONS);
        return false;
    }

    ascii_defer_action_t *action = &scope->actions[scope->count];
    action->fn = fn;
    action->context_size = context_size;

    // Allocate and copy context if needed
    if (context_size > 0 && context) {
        action->context = SAFE_MALLOC(context_size, void*);
        if (!action->context) {
            log_error("ascii_defer_push: Failed to allocate context (%zu bytes)",
                      context_size);
            return false;
        }
        memcpy(action->context, context, context_size);
    } else {
        action->context = NULL;
    }

    scope->count++;

    log_debug("Registered defer action %zu/%d (fn=%p, ctx_size=%zu)",
              scope->count, ASCII_DEFER_MAX_ACTIONS, (void*)fn, context_size);

    return true;
}

void ascii_defer_execute_all(ascii_defer_scope_t *scope) {
    if (!scope) {
        return;
    }

    if (scope->executed) {
        log_warn("ascii_defer_execute_all: Scope already executed");
        return;
    }

    // Mark as executed before running actions to prevent re-execution
    scope->executed = true;

    log_debug("Executing %zu deferred actions in LIFO order", scope->count);

    // Execute in LIFO order (last registered, first executed)
    for (size_t i = scope->count; i > 0; i--) {
        ascii_defer_action_t *action = &scope->actions[i - 1];

        if (action->fn) {
            log_debug("Executing defer action %zu (fn=%p, ctx=%p)",
                      i, (void*)action->fn, action->context);

            // Call the deferred function
            action->fn(action->context);
        }

        // Free context memory
        if (action->context) {
            free(action->context);
            action->context = NULL;
        }
    }

    scope->count = 0;
}
