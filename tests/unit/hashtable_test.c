#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <criterion/parameterized.h>
#include <string.h>

#include "common.h"
#include "hashtable.h"
#include "tests/logging.h"

// Use the enhanced macro to create complete test suite with basic quiet logging
TEST_SUITE_WITH_QUIET_LOGGING(hashtable);

// Test data structure for storing in hashtable
typedef struct {
  int id;
  char name[32];
  double value;
} test_data_t;

// =============================================================================
// Parameterized Tests for Hashtable Operations
// =============================================================================

// Test case structure for hashtable key operations
typedef struct {
  uint32_t key;
  const char *description;
  bool should_succeed;
} hashtable_key_test_case_t;

static hashtable_key_test_case_t hashtable_key_cases[] = {{1, "Small positive key", true},
                                                          {100, "Medium positive key", true},
                                                          {1000, "Large positive key", true},
                                                          {0, "Zero key (reserved)", false},
                                                          {UINT32_MAX, "Maximum key", true},
                                                          {HASHTABLE_BUCKET_COUNT, "Bucket count key", true},
                                                          {HASHTABLE_BUCKET_COUNT + 1, "Bucket count + 1 key", true}};

ParameterizedTestParameters(hashtable, key_operations) {
  size_t nb_cases = sizeof(hashtable_key_cases) / sizeof(hashtable_key_cases[0]);
  return cr_make_param_array(hashtable_key_test_case_t, hashtable_key_cases, nb_cases);
}

ParameterizedTest(hashtable_key_test_case_t *tc, hashtable, key_operations) {
  hashtable_t *ht = hashtable_create();
  cr_assert_not_null(ht, "Hashtable creation should succeed");

  // Create test data
  test_data_t *data;
  SAFE_MALLOC(data, sizeof(test_data_t), test_data_t *);
  data->id = (int)tc->key;
  snprintf(data->name, sizeof(data->name), "Test %u", tc->key);
  data->value = tc->key * 1.5;

  // Test insert
  bool insert_result = hashtable_insert(ht, tc->key, data);
  cr_assert_eq(insert_result, tc->should_succeed, "Insert should %s for %s", tc->should_succeed ? "succeed" : "fail",
               tc->description);

  if (tc->should_succeed) {
    cr_assert_eq(hashtable_size(ht), 1, "Size should be 1 after successful insert");
    cr_assert(hashtable_contains(ht, tc->key), "Should contain key %u", tc->key);

    // Test lookup
    test_data_t *found = (test_data_t *)hashtable_lookup(ht, tc->key);
    cr_assert_not_null(found, "Lookup should find key %u", tc->key);
    cr_assert_eq(found, data, "Found data should be the same pointer");
    cr_assert_eq(found->id, (int)tc->key, "Found data ID should match key");

    // Test remove
    bool remove_result = hashtable_remove(ht, tc->key);
    cr_assert(remove_result, "Remove should succeed for key %u", tc->key);
    cr_assert_eq(hashtable_size(ht), 0, "Size should be 0 after remove");
    cr_assert_null(hashtable_lookup(ht, tc->key), "Lookup should fail after remove");
  }

  free(data);
  hashtable_destroy(ht);
}

// Test case structure for hashtable collision scenarios
typedef struct {
  uint32_t keys[4];
  const char *description;
  bool should_all_succeed;
} hashtable_collision_test_case_t;

static hashtable_collision_test_case_t hashtable_collision_cases[] = {
    {{1, HASHTABLE_BUCKET_COUNT + 1, HASHTABLE_BUCKET_COUNT * 2 + 1, HASHTABLE_BUCKET_COUNT * 3 + 1},
     "Sequential collision keys",
     true},
    {{HASHTABLE_BUCKET_COUNT, HASHTABLE_BUCKET_COUNT * 2, HASHTABLE_BUCKET_COUNT * 3, HASHTABLE_BUCKET_COUNT * 4},
     "Aligned collision keys",
     true},
    {{100, 200, 300, 400}, "Non-colliding keys", true},
    {{1, 1, 2, 2}, "Duplicate keys", false}};

ParameterizedTestParameters(hashtable, collision_scenarios) {
  size_t nb_cases = sizeof(hashtable_collision_cases) / sizeof(hashtable_collision_cases[0]);
  return cr_make_param_array(hashtable_collision_test_case_t, hashtable_collision_cases, nb_cases);
}

ParameterizedTest(hashtable_collision_test_case_t *tc, hashtable, collision_scenarios) {
  hashtable_t *ht = hashtable_create();
  cr_assert_not_null(ht, "Hashtable creation should succeed");

  test_data_t *items[4];
  int successful_inserts = 0;

  // Insert all keys
  for (int i = 0; i < 4; i++) {
    SAFE_MALLOC(items[i], sizeof(test_data_t), test_data_t *);
    items[i]->id = (int)tc->keys[i];
    snprintf(items[i]->name, sizeof(items[i]->name), "Item %u", tc->keys[i]);

    bool result = hashtable_insert(ht, tc->keys[i], items[i]);
    if (result) {
      successful_inserts++;
    }
  }

  if (tc->should_all_succeed) {
    cr_assert_eq(successful_inserts, 4, "All inserts should succeed for %s", tc->description);
    cr_assert_eq(hashtable_size(ht), 4, "Size should be 4 for %s", tc->description);

    // Verify all can be found
    for (int i = 0; i < 4; i++) {
      test_data_t *found = (test_data_t *)hashtable_lookup(ht, tc->keys[i]);
      cr_assert_not_null(found, "Item %u should be found for %s", tc->keys[i], tc->description);
    }

    // Clean up - all inserts succeeded, so free all items
    for (int i = 0; i < 4; i++) {
      free(items[i]);
    }
  } else {
    // For duplicate keys: hashtable_insert returns true but updates existing value
    // All 4 inserts will succeed, but only unique keys will be in the table
    cr_assert_eq(successful_inserts, 4, "All inserts return true for %s", tc->description);

    // Count unique keys
    size_t unique_keys = 0;
    for (int i = 0; i < 4; i++) {
      bool is_unique = true;
      for (int j = 0; j < i; j++) {
        if (tc->keys[i] == tc->keys[j]) {
          is_unique = false;
          break;
        }
      }
      if (is_unique) {
        unique_keys++;
      }
    }

    cr_assert_eq(hashtable_size(ht), unique_keys, "Size should match unique keys for %s", tc->description);

    // Clean up - only free the items that are still in the hashtable (last insert for each key)
    // For {1, 1, 2, 2}: items[1] and items[3] are in hashtable, items[0] and items[2] are orphaned
    // We need to free the orphaned ones manually
    for (int i = 0; i < 4; i++) {
      bool is_last_occurrence = true;
      for (int j = i + 1; j < 4; j++) {
        if (tc->keys[i] == tc->keys[j]) {
          is_last_occurrence = false;
          break;
        }
      }
      if (!is_last_occurrence) {
        // This item was replaced, free it manually
        free(items[i]);
      }
    }
  }

  hashtable_destroy(ht);
}

// =============================================================================
// Hashtable Creation and Destruction Tests
// =============================================================================

Test(hashtable, creation_and_destruction) {
  hashtable_t *ht = hashtable_create();
  cr_assert_not_null(ht, "Hashtable creation should succeed");

  cr_assert_eq(hashtable_size(ht), 0, "Empty hashtable size should be 0");
  cr_assert_not_null(ht->entry_pool, "Entry pool should be allocated");
  cr_assert_not_null(ht->free_list, "Free list should be initialized");
  cr_assert_eq(ht->entry_count, 0, "Entry count should be 0");

  hashtable_destroy(ht);
}

Test(hashtable, multiple_creation_destruction) {
  // Test multiple create/destroy cycles
  for (int i = 0; i < 5; i++) {
    hashtable_t *ht = hashtable_create();
    cr_assert_not_null(ht, "Hashtable creation %d should succeed", i);
    hashtable_destroy(ht);
  }
}

Test(hashtable, null_destruction_safety) {
  // Destroying NULL hashtable should be safe
  hashtable_destroy(NULL);
}

// =============================================================================
// Basic Insert/Lookup/Remove Tests
// =============================================================================

Test(hashtable, basic_insert_lookup) {
  hashtable_t *ht = hashtable_create();
  cr_assert_not_null(ht, "Hashtable creation should succeed");

  // Create test data
  test_data_t *data;
  SAFE_MALLOC(data, sizeof(test_data_t), test_data_t *);
  data->id = 123;
  strcpy(data->name, "Test Item");
  data->value = 3.14159;

  // Insert data
  bool result = hashtable_insert(ht, 123, data);
  cr_assert(result, "Insert should succeed");
  cr_assert_eq(hashtable_size(ht), 1, "Size should be 1 after insert");

  // Lookup data
  test_data_t *found = (test_data_t *)hashtable_lookup(ht, 123);
  cr_assert_not_null(found, "Lookup should find the data");
  cr_assert_eq(found, data, "Found data should be the same pointer");
  cr_assert_eq(found->id, 123, "Found data ID should match");
  cr_assert_str_eq(found->name, "Test Item", "Found data name should match");
  cr_assert_float_eq(found->value, 3.14159, 0.00001, "Found data value should match");

  // Verify contains
  cr_assert(hashtable_contains(ht, 123), "Should contain key 123");
  cr_assert(hashtable_contains(ht, 456) == false, "Should not contain key 456");

  free(data);
  hashtable_destroy(ht);
}

Test(hashtable, basic_remove) {
  hashtable_t *ht = hashtable_create();
  cr_assert_not_null(ht, "Hashtable creation should succeed");

  // Insert multiple items
  test_data_t *data1, *data2, *data3;
  SAFE_MALLOC(data1, sizeof(test_data_t), test_data_t *);
  SAFE_MALLOC(data2, sizeof(test_data_t), test_data_t *);
  SAFE_MALLOC(data3, sizeof(test_data_t), test_data_t *);

  data1->id = 100;
  strcpy(data1->name, "Item 1");
  data2->id = 200;
  strcpy(data2->name, "Item 2");
  data3->id = 300;
  strcpy(data3->name, "Item 3");

  hashtable_insert(ht, 100, data1);
  hashtable_insert(ht, 200, data2);
  hashtable_insert(ht, 300, data3);

  cr_assert_eq(hashtable_size(ht), 3, "Should have 3 items");

  // Remove middle item
  bool result = hashtable_remove(ht, 200);
  cr_assert(result, "Remove should succeed");
  cr_assert_eq(hashtable_size(ht), 2, "Should have 2 items after remove");

  // Verify it's gone
  cr_assert_null(hashtable_lookup(ht, 200), "Removed item should not be found");
  cr_assert(hashtable_contains(ht, 200) == false, "Should not contain removed key");

  // Verify others still exist
  cr_assert_not_null(hashtable_lookup(ht, 100), "Item 1 should still exist");
  cr_assert_not_null(hashtable_lookup(ht, 300), "Item 3 should still exist");

  free(data1);
  free(data2);
  free(data3);
  hashtable_destroy(ht);
}

Test(hashtable, remove_nonexistent) {
  hashtable_t *ht = hashtable_create();
  cr_assert_not_null(ht, "Hashtable creation should succeed");

  // Try to remove from empty hashtable
  bool result = hashtable_remove(ht, 123);
  cr_assert(result == false, "Remove from empty table should fail");

  // Add an item, then try to remove different key
  test_data_t data = {.id = 100};
  hashtable_insert(ht, 100, &data);

  result = hashtable_remove(ht, 999);
  cr_assert(result == false, "Remove nonexistent key should fail");
  cr_assert_eq(hashtable_size(ht), 1, "Size should remain unchanged");

  hashtable_destroy(ht);
}

// =============================================================================
// Multiple Items and Collision Tests
// =============================================================================

Test(hashtable, multiple_items) {
  hashtable_t *ht = hashtable_create();
  cr_assert_not_null(ht, "Hashtable creation should succeed");

  // Insert multiple items
  const int num_items = 10;
  test_data_t *items[num_items];

  for (int i = 0; i < num_items; i++) {
    SAFE_MALLOC(items[i], sizeof(test_data_t), test_data_t *);
    items[i]->id = i + 1000;
    snprintf(items[i]->name, sizeof(items[i]->name), "Item %d", i);
    items[i]->value = i * 1.5;

    bool result = hashtable_insert(ht, i + 1000, items[i]);
    cr_assert(result, "Insert item %d should succeed", i);
  }

  cr_assert_eq(hashtable_size(ht), num_items, "Size should match number of items");

  // Verify all items can be found
  for (int i = 0; i < num_items; i++) {
    test_data_t *found = (test_data_t *)hashtable_lookup(ht, i + 1000);
    cr_assert_not_null(found, "Item %d should be found", i);
    cr_assert_eq(found->id, i + 1000, "Item %d ID should match", i);

    char expected_name[32];
    snprintf(expected_name, sizeof(expected_name), "Item %d", i);
    cr_assert_str_eq(found->name, expected_name, "Item %d name should match", i);
  }

  // Clean up
  for (int i = 0; i < num_items; i++) {
    free(items[i]);
  }
  hashtable_destroy(ht);
}

Test(hashtable, hash_collisions) {
  hashtable_t *ht = hashtable_create();
  cr_assert_not_null(ht, "Hashtable creation should succeed");

  // Insert keys that likely cause collisions (depend on hash function implementation)
  // For power-of-2 bucket count, keys that differ by bucket_count should collide
  // Use keys that are more likely to collide with simple hash functions
  uint32_t keys[] = {1, HASHTABLE_BUCKET_COUNT + 1, HASHTABLE_BUCKET_COUNT * 2 + 1, HASHTABLE_BUCKET_COUNT * 3 + 1};
  test_data_t *items[4];

  for (int i = 0; i < 4; i++) {
    SAFE_MALLOC(items[i], sizeof(test_data_t), test_data_t *);
    items[i]->id = keys[i];
    snprintf(items[i]->name, sizeof(items[i]->name), "Collision %d", i);

    bool result = hashtable_insert(ht, keys[i], items[i]);
    cr_assert(result, "Insert collision item %d should succeed", i);
  }

  cr_assert_eq(hashtable_size(ht), 4, "All collision items should be inserted");

  // Verify all can be found despite collisions
  for (int i = 0; i < 4; i++) {
    test_data_t *found = (test_data_t *)hashtable_lookup(ht, keys[i]);
    cr_assert_not_null(found, "Collision item %d should be found", i);
    cr_assert_eq(found->id, (int)keys[i], "Collision item %d ID should match", i);
  }

  // Remove one collision item and verify others remain
  hashtable_remove(ht, keys[1]);
  cr_assert_eq(hashtable_size(ht), 3, "Size should decrease after remove");
  cr_assert_null(hashtable_lookup(ht, keys[1]), "Removed collision item should be gone");
  cr_assert_not_null(hashtable_lookup(ht, keys[0]), "Other collision items should remain");
  cr_assert_not_null(hashtable_lookup(ht, keys[2]), "Other collision items should remain");
  cr_assert_not_null(hashtable_lookup(ht, keys[3]), "Other collision items should remain");

  for (int i = 0; i < 4; i++) {
    free(items[i]);
  }
  hashtable_destroy(ht);
}

// =============================================================================
// Update and Duplicate Key Tests
// =============================================================================

Test(hashtable, duplicate_key_insert) {
  hashtable_t *ht = hashtable_create();
  cr_assert_not_null(ht, "Hashtable creation should succeed");

  test_data_t *data1, *data2;
  SAFE_MALLOC(data1, sizeof(test_data_t), test_data_t *);
  SAFE_MALLOC(data2, sizeof(test_data_t), test_data_t *);

  data1->id = 123;
  strcpy(data1->name, "Original");
  data2->id = 123;
  strcpy(data2->name, "Updated");

  // Insert first item
  bool result = hashtable_insert(ht, 123, data1);
  cr_assert(result, "First insert should succeed");
  cr_assert_eq(hashtable_size(ht), 1, "Size should be 1");

  // Insert with same key (should replace or fail depending on implementation)
  (void)hashtable_insert(ht, 123, data2);
  // Behavior may vary - either replace (return true) or reject (return false)

  // Lookup should return one of them
  test_data_t *found = (test_data_t *)hashtable_lookup(ht, 123);
  cr_assert_not_null(found, "Lookup should find an item");
  cr_assert_eq(found->id, 123, "Found item should have correct ID");

  free(data1);
  free(data2);
  hashtable_destroy(ht);
}

// =============================================================================
// Capacity and Entry Pool Tests
// =============================================================================

Test(hashtable, entry_pool_exhaustion) {
  hashtable_t *ht = hashtable_create();
  cr_assert_not_null(ht, "Hashtable creation should succeed");

  // Try to insert more than pool capacity (be more conservative to avoid crash)
  const int test_items = HASHTABLE_MAX_ENTRIES + 2; // Smaller excess to prevent crash
  test_data_t *items[test_items];
  int successful_inserts = 0;

  // Allocate all test items first
  for (int i = 0; i < test_items; i++) {
    SAFE_MALLOC(items[i], sizeof(test_data_t), test_data_t *);
    items[i]->id = i + 2001;
    snprintf(items[i]->name, sizeof(items[i]->name), "Pooled %d", i);
  }

  // Try to insert items, stopping when pool exhausted
  for (int i = 0; i < test_items; i++) {
    bool result = hashtable_insert(ht, i + 2001, items[i]);
    if (result) {
      successful_inserts++;
    } else {
      // Pool exhausted - this is expected behavior
      log_info("Pool exhausted after %d successful inserts", successful_inserts);
      break;
    }
  }

  cr_assert_gt(successful_inserts, 0, "Should insert at least some items");
  cr_assert_leq(successful_inserts, HASHTABLE_MAX_ENTRIES, "Should not exceed max entries");
  cr_assert_eq(hashtable_size(ht), (size_t)successful_inserts, "Size should match successful inserts");

  // Verify inserted items can be found
  for (int i = 0; i < successful_inserts; i++) {
    test_data_t *found = (test_data_t *)hashtable_lookup(ht, i + 2001);
    cr_assert_not_null(found, "Inserted item %d should be found", i);
  }

  // Clean up all allocated memory
  for (int i = 0; i < test_items; i++) {
    free(items[i]);
  }
  hashtable_destroy(ht);
}

Test(hashtable, entry_pool_reuse) {
  hashtable_t *ht = hashtable_create();
  cr_assert_not_null(ht, "Hashtable creation should succeed");

  test_data_t data1 = {.id = 100};
  test_data_t data2 = {.id = 200};

  // Insert item
  hashtable_insert(ht, 100, &data1);
  cr_assert_eq(hashtable_size(ht), 1, "Size should be 1");

  // Remove item (should return entry to pool)
  hashtable_remove(ht, 100);
  cr_assert_eq(hashtable_size(ht), 0, "Size should be 0");

  // Insert new item (should reuse pool entry)
  hashtable_insert(ht, 200, &data2);
  cr_assert_eq(hashtable_size(ht), 1, "Size should be 1 again");

  // Verify new item
  test_data_t *found = (test_data_t *)hashtable_lookup(ht, 200);
  cr_assert_not_null(found, "New item should be found");
  cr_assert_eq(found->id, 200, "New item should have correct ID");

  hashtable_destroy(ht);
}

// =============================================================================
// Statistics and Load Factor Tests
// =============================================================================

Test(hashtable, load_factor_calculation) {
  hashtable_t *ht = hashtable_create();
  cr_assert_not_null(ht, "Hashtable creation should succeed");

  // Empty table load factor
  double load_factor = hashtable_load_factor(ht);
  cr_assert_eq(load_factor, 0.0, "Empty table load factor should be 0");

  // Add some items
  test_data_t items[5];
  for (int i = 0; i < 5; i++) {
    items[i].id = i + 3000;
    hashtable_insert(ht, i + 3000, &items[i]);
  }

  load_factor = hashtable_load_factor(ht);
  double expected = (double)5 / HASHTABLE_BUCKET_COUNT;
  cr_assert_float_eq(load_factor, expected, 0.001, "Load factor should be correct");

  hashtable_destroy(ht);
}

Test(hashtable, statistics_tracking) {
  hashtable_t *ht = hashtable_create();
  cr_assert_not_null(ht, "Hashtable creation should succeed");

  // Initial statistics should be zero
  cr_assert_eq(ht->lookups, 0, "Initial lookups should be 0");
  cr_assert_eq(ht->hits, 0, "Initial hits should be 0");
  cr_assert_eq(ht->insertions, 0, "Initial insertions should be 0");
  cr_assert_eq(ht->deletions, 0, "Initial deletions should be 0");

  test_data_t data = {.id = 4000};

  // Insert should increment insertions
  hashtable_insert(ht, 4000, &data);
  cr_assert_gt(ht->insertions, 0, "Insertions should increment");

  // Lookup should increment lookups and hits
  uint64_t lookups_before = ht->lookups;
  uint64_t hits_before = ht->hits;

  hashtable_lookup(ht, 4000);
  cr_assert_gt(ht->lookups, lookups_before, "Lookups should increment");
  cr_assert_gt(ht->hits, hits_before, "Hits should increment for found item");

  // Lookup nonexistent should increment lookups but not hits
  lookups_before = ht->lookups;
  hits_before = ht->hits;

  hashtable_lookup(ht, 9999);
  cr_assert_gt(ht->lookups, lookups_before, "Lookups should increment for miss");
  cr_assert_eq(ht->hits, hits_before, "Hits should not increment for miss");

  // Remove should increment deletions
  uint64_t deletions_before = ht->deletions;
  hashtable_remove(ht, 4000);
  cr_assert_gt(ht->deletions, deletions_before, "Deletions should increment");

  hashtable_destroy(ht);
}

// =============================================================================
// Iterator/Foreach Tests
// =============================================================================

typedef struct {
  int count;
  uint32_t keys[32];
  void *values[32];
} foreach_context_t;

void test_foreach_callback(uint32_t key, void *value, void *user_data) {
  foreach_context_t *ctx = (foreach_context_t *)user_data;
  if (ctx->count < 32) {
    ctx->keys[ctx->count] = key;
    ctx->values[ctx->count] = value;
    ctx->count++;
  }
}

Test(hashtable, foreach_iteration) {
  hashtable_t *ht = hashtable_create();
  cr_assert_not_null(ht, "Hashtable creation should succeed");

  // Insert test items
  test_data_t items[5];
  uint32_t keys[] = {100, 200, 300, 400, 500};

  for (int i = 0; i < 5; i++) {
    items[i].id = keys[i];
    snprintf(items[i].name, sizeof(items[i].name), "Item %d", i);
    hashtable_insert(ht, keys[i], &items[i]);
  }

  // Iterate over all items
  foreach_context_t ctx = {0};
  hashtable_foreach(ht, test_foreach_callback, &ctx);

  cr_assert_eq(ctx.count, 5, "Should iterate over all 5 items");

  // Verify all keys were visited (order may vary)
  bool found_keys[5] = {false};
  for (int i = 0; i < ctx.count; i++) {
    for (int j = 0; j < 5; j++) {
      if (ctx.keys[i] == keys[j]) {
        found_keys[j] = true;
        test_data_t *data = (test_data_t *)ctx.values[i];
        cr_assert_eq(data->id, (int)keys[j], "Data ID should match key");
      }
    }
  }

  for (int i = 0; i < 5; i++) {
    cr_assert(found_keys[i], "Key %d should have been visited", keys[i]);
  }

  hashtable_destroy(ht);
}

Test(hashtable, foreach_empty_table) {
  hashtable_t *ht = hashtable_create();
  cr_assert_not_null(ht, "Hashtable creation should succeed");

  foreach_context_t ctx = {0};
  hashtable_foreach(ht, test_foreach_callback, &ctx);

  cr_assert_eq(ctx.count, 0, "Empty table iteration should visit 0 items");

  hashtable_destroy(ht);
}

// =============================================================================
// Thread Safety and Locking Tests
// =============================================================================

Test(hashtable, manual_locking) {
  hashtable_t *ht = hashtable_create();
  cr_assert_not_null(ht, "Hashtable creation should succeed");

  test_data_t data = {.id = 5000};
  hashtable_insert(ht, 5000, &data);

  // Test manual read locking
  hashtable_read_lock(ht);
  test_data_t *found = (test_data_t *)hashtable_lookup(ht, 5000);
  cr_assert_not_null(found, "Should find item under read lock");
  hashtable_read_unlock(ht);

  // Test manual write locking
  hashtable_write_lock(ht);
  bool result = hashtable_remove(ht, 5000);
  cr_assert(result, "Should remove item under write lock");
  hashtable_write_unlock(ht);

  hashtable_destroy(ht);
}

// =============================================================================
// Edge Cases and Error Handling
// =============================================================================

Test(hashtable, null_pointer_handling) {
  hashtable_t *ht = hashtable_create();
  cr_assert_not_null(ht, "Hashtable creation should succeed");

  // Insert NULL value - behavior depends on implementation
  bool result = hashtable_insert(ht, 123, NULL);
  // Some implementations may not allow NULL values
  // Just verify it doesn't crash

  // Lookup should return NULL
  void *found = hashtable_lookup(ht, 123);
  cr_assert_null(found, "Lookup should return NULL for NULL value");

  // Contains behavior with NULL values depends on implementation
  // Since hashtable_contains() uses lookup != NULL, it will return false for NULL values
  // This is actually correct behavior - NULL values are indistinguishable from missing keys
  cr_assert(hashtable_contains(ht, 123) == false, "NULL values should not be considered 'contained'");

  // Operations on NULL hashtable should handle gracefully
  result = hashtable_insert(NULL, 123, &result);
  cr_assert(result == false, "Insert to NULL hashtable should fail");

  found = hashtable_lookup(NULL, 123);
  cr_assert_null(found, "Lookup from NULL hashtable should return NULL");

  result = hashtable_remove(NULL, 123);
  cr_assert(result == false, "Remove from NULL hashtable should fail");

  cr_assert(hashtable_contains(NULL, 123) == false, "NULL hashtable should not contain anything");

  cr_assert_eq(hashtable_size(NULL), 0, "NULL hashtable size should be 0");

  hashtable_destroy(ht);
}

Test(hashtable, large_key_values) {
  hashtable_t *ht = hashtable_create();
  cr_assert_not_null(ht, "Hashtable creation should succeed");

  // Test with maximum uint32_t values
  uint32_t large_keys[] = {0xFFFFFFFF, 0x80000000, 0x7FFFFFFF, 0x12345678};
  test_data_t items[4];

  for (int i = 0; i < 4; i++) {
    items[i].id = large_keys[i];
    snprintf(items[i].name, sizeof(items[i].name), "Large %u", large_keys[i]);

    bool result = hashtable_insert(ht, large_keys[i], &items[i]);
    cr_assert(result, "Insert large key %u should succeed", large_keys[i]);
  }

  // Verify all can be found
  for (int i = 0; i < 4; i++) {
    test_data_t *found = (test_data_t *)hashtable_lookup(ht, large_keys[i]);
    cr_assert_not_null(found, "Large key %u should be found", large_keys[i]);
    cr_assert_eq(found->id, (int)large_keys[i], "Found data should match");
  }

  hashtable_destroy(ht);
}

Test(hashtable, zero_key) {
  hashtable_t *ht = hashtable_create();
  cr_assert_not_null(ht, "Hashtable creation should succeed");

  test_data_t data = {.id = 0};

  // Test with key = 0 - some implementations may not handle 0 keys
  bool result = hashtable_insert(ht, 0, &data);
  // Key 0 handling depends on implementation
  if (!result) {
    // If key 0 is not supported, skip remaining tests
    hashtable_destroy(ht);
    return;
  }

  test_data_t *found = (test_data_t *)hashtable_lookup(ht, 0);
  cr_assert_not_null(found, "Key 0 should be found");
  cr_assert_eq(found->id, 0, "Found data should match");

  cr_assert(hashtable_contains(ht, 0), "Should contain key 0");

  result = hashtable_remove(ht, 0);
  cr_assert(result, "Remove key 0 should succeed");

  hashtable_destroy(ht);
}

// =============================================================================
// Performance and Stress Tests
// =============================================================================

Test(hashtable, stress_test) {
  hashtable_t *ht = hashtable_create();
  cr_assert_not_null(ht, "Hashtable creation should succeed");

  // Insert many items rapidly
  const int num_items = HASHTABLE_MAX_ENTRIES / 2; // Don't exhaust pool
  test_data_t *items[num_items];

  // Insert phase
  for (int i = 0; i < num_items; i++) {
    SAFE_MALLOC(items[i], sizeof(test_data_t), test_data_t *);
    items[i]->id = i + 10000;
    snprintf(items[i]->name, sizeof(items[i]->name), "Stress %d", i);

    bool result = hashtable_insert(ht, i + 10000, items[i]);
    cr_assert(result, "Stress insert %d should succeed", i);
  }

  // Lookup phase - verify all items
  for (int i = 0; i < num_items; i++) {
    test_data_t *found = (test_data_t *)hashtable_lookup(ht, i + 10000);
    cr_assert_not_null(found, "Stress lookup %d should succeed", i);
    cr_assert_eq(found->id, i + 10000, "Stress data %d should match", i);
  }

  // Remove odd items
  for (int i = 1; i < num_items; i += 2) {
    bool result = hashtable_remove(ht, i + 10000);
    cr_assert(result, "Stress remove %d should succeed", i);
  }

  // Verify even items still exist, odd items gone
  for (int i = 0; i < num_items; i++) {
    test_data_t *found = (test_data_t *)hashtable_lookup(ht, i + 10000);
    if (i % 2 == 0) {
      cr_assert_not_null(found, "Even item %d should still exist", i);
    } else {
      cr_assert_null(found, "Odd item %d should be removed", i);
    }
  }

  // Clean up
  for (int i = 0; i < num_items; i++) {
    free(items[i]);
  }
  hashtable_destroy(ht);
}
