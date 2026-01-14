/**
 * @file acds/session.c
 * @brief 🎯 Session data structure utilities
 *
 * Minimal session utilities. All session operations are handled
 * directly by the database layer (database.c) using SQLite.
 */

#include "acds/session.h"

/**
 * @brief Free a session entry and all its resources
 * @param entry Session entry to free
 */
void session_entry_free(session_entry_t *entry) {
  if (!entry) {
    return;
  }

  // Free all participants
  for (size_t i = 0; i < MAX_PARTICIPANTS; i++) {
    if (entry->participants[i]) {
      SAFE_FREE(entry->participants[i]);
    }
  }

  // Free the entry itself
  SAFE_FREE(entry);
}
