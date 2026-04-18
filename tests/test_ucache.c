#include "unity.h"
#include "uobject.h"
#include "ucache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================
// Test helpers
// ============================================================

typedef struct CacheValue {
    uobject obj;
    int id;
} CacheValue;

static void cache_value_release(uobject *obj) {
    CacheValue *v = uobject_cast(obj, CacheValue, obj);
    free(v);
}

static const uobject_type cache_value_type = {
    .name = "CacheValue",
    .size = sizeof(CacheValue),
    .release = cache_value_release,
};

static CacheValue *cache_value_new(int id) {
    CacheValue *v = malloc(sizeof(CacheValue));
    TEST_ASSERT_NOT_NULL(v);
    v->id = id;
    char name[32];
    snprintf(name, sizeof(name), "val_%d", id);
    uobject_init(&v->obj, &cache_value_type, strdup(name));
    return v;
}

// Tracked value — records how many times the destructor was called globally.
typedef struct TrackedValue {
    uobject obj;
    int id;
    int *destroy_flag;   // Points to a caller-owned counter; set to 1 on destroy
} TrackedValue;

static void tracked_value_release(uobject *obj) {
    TrackedValue *v = uobject_cast(obj, TrackedValue, obj);
    if (v->destroy_flag) {
        *v->destroy_flag = 1;
    }
    free(v);
}

static const uobject_type tracked_value_type = {
    .name = "TrackedValue",
    .size = sizeof(TrackedValue),
    .release = tracked_value_release,
};

static TrackedValue *tracked_value_new(int id, int *destroy_flag) {
    TrackedValue *v = malloc(sizeof(TrackedValue));
    TEST_ASSERT_NOT_NULL(v);
    v->id = id;
    v->destroy_flag = destroy_flag;
    *destroy_flag = 0;
    char name[32];
    snprintf(name, sizeof(name), "tracked_%d", id);
    uobject_init(&v->obj, &tracked_value_type, strdup(name));
    return v;
}

static ucache_config basic_config(void) {
    return (ucache_config){
        .max_items = 100,
        .max_memory = 0,
        .initial_capacity = 0,
        .thread_safe = false,
        .enable_stats = true,
    };
}

// ============================================================
// Tests
// ============================================================

static void test_ucache_new_basic(void) {
    ucache_config config = basic_config();
    ucache *cache = ucache_new(&config);
    TEST_ASSERT_NOT_NULL(cache);
    ucache_free(cache);
}

static void test_ucache_new_null_config(void) {
    ucache *cache = ucache_new(NULL);
    TEST_ASSERT_NULL(cache);
}

static void test_ucache_new_no_limits(void) {
    ucache_config config = {
        .max_items = 0,
        .max_memory = 0,
    };
    ucache *cache = ucache_new(&config);
    TEST_ASSERT_NULL(cache);
}

static void test_ucache_new_custom_capacity(void) {
    ucache_config config = {
        .max_items = 100,
        .max_memory = 0,
        .initial_capacity = 64,
        .thread_safe = false,
        .enable_stats = false,
    };
    ucache *cache = ucache_new(&config);
    TEST_ASSERT_NOT_NULL(cache);
    ucache_free(cache);
}

static void test_ucache_set_get_basic(void) {
    ucache_config cfg = basic_config();
    ucache *cache = ucache_new(&cfg);
    CacheValue *v = cache_value_new(1);

    ucache_result r = ucache_set(cache, "key1", 4, &v->obj);
    TEST_ASSERT_EQUAL(UCACHE_OK, r);

    uobject *result = NULL;
    r = ucache_get_retain(cache, "key1", 4, &result);
    TEST_ASSERT_EQUAL(UCACHE_OK, r);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_PTR(&v->obj, result);

    uobject_release(result);  // release the get_retain reference
    uobject_release(&v->obj); // release our original reference
    ucache_free(cache);
}

static void test_ucache_set_update_existing(void) {
    ucache_config cfg = basic_config();
    ucache *cache = ucache_new(&cfg);
    CacheValue *v1 = cache_value_new(1);
    CacheValue *v2 = cache_value_new(2);

    ucache_set(cache, "key", 3, &v1->obj);
    ucache_set(cache, "key", 3, &v2->obj);

    uobject *result = NULL;
    ucache_get_retain(cache, "key", 3, &result);
    TEST_ASSERT_EQUAL_PTR(&v2->obj, result);

    uobject_release(result);
    uobject_release(&v1->obj);
    uobject_release(&v2->obj);
    ucache_free(cache);
}

static void test_ucache_get_not_found(void) {
    ucache_config cfg = basic_config();
    ucache *cache = ucache_new(&cfg);

    uobject *result = (void *)0xDEAD;
    ucache_result r = ucache_get_retain(cache, "nokey", 5, &result);
    TEST_ASSERT_EQUAL(UCACHE_ERR_NOT_FOUND, r);
    TEST_ASSERT_NULL(result);

    ucache_free(cache);
}

static void test_ucache_get_retain_refcount(void) {
    ucache_config cfg = basic_config();
    ucache *cache = ucache_new(&cfg);
    CacheValue *v = cache_value_new(1);

    ucache_set(cache, "key", 3, &v->obj);
    TEST_ASSERT_EQUAL_UINT32(2, uobject_refcount(&v->obj)); // 1 ours + 1 cache

    uobject *result = NULL;
    ucache_get_retain(cache, "key", 3, &result);
    TEST_ASSERT_EQUAL_UINT32(3, uobject_refcount(&v->obj)); // +1 from get_retain

    uobject_release(result); // release get_retain
    TEST_ASSERT_EQUAL_UINT32(2, uobject_refcount(&v->obj));

    uobject_release(&v->obj); // release our reference
    ucache_free(cache);       // cache releases its reference -> destroys
}

static void test_ucache_exists(void) {
    ucache_config cfg = basic_config();
    ucache *cache = ucache_new(&cfg);
    CacheValue *v = cache_value_new(1);

    ucache_set(cache, "key", 3, &v->obj);

    bool exists = false;
    ucache_exists(cache, "key", 3, &exists);
    TEST_ASSERT_TRUE(exists);

    ucache_exists(cache, "nokey", 5, &exists);
    TEST_ASSERT_FALSE(exists);

    uobject_release(&v->obj);
    ucache_free(cache);
}

static void test_ucache_delete(void) {
    ucache_config cfg = basic_config();
    ucache *cache = ucache_new(&cfg);
    CacheValue *v = cache_value_new(1);

    ucache_set(cache, "key", 3, &v->obj);
    ucache_result r = ucache_delete(cache, "key", 3);
    TEST_ASSERT_EQUAL(UCACHE_OK, r);

    bool exists = true;
    ucache_exists(cache, "key", 3, &exists);
    TEST_ASSERT_FALSE(exists);

    uobject_release(&v->obj);
    ucache_free(cache);
}

static void test_ucache_delete_not_found(void) {
    ucache_config cfg = basic_config();
    ucache *cache = ucache_new(&cfg);

    ucache_result r = ucache_delete(cache, "nokey", 5);
    TEST_ASSERT_EQUAL(UCACHE_ERR_NOT_FOUND, r);

    ucache_free(cache);
}

static void test_ucache_clear(void) {
    ucache_config cfg = basic_config();
    ucache *cache = ucache_new(&cfg);
    CacheValue *v1 = cache_value_new(1);
    CacheValue *v2 = cache_value_new(2);

    ucache_set(cache, "k1", 2, &v1->obj);
    ucache_set(cache, "k2", 2, &v2->obj);

    ucache_result r = ucache_clear(cache);
    TEST_ASSERT_EQUAL(UCACHE_OK, r);

    ucache_stats stats;
    ucache_get_stats(cache, &stats);
    TEST_ASSERT_EQUAL_UINT64(0, stats.item_count);

    uobject_release(&v1->obj);
    uobject_release(&v2->obj);
    ucache_free(cache);
}

static void test_ucache_eviction_by_items(void) {
    ucache_config config = {
        .max_items = 3,
        .max_memory = 0,
        .thread_safe = false,
        .enable_stats = true,
    };
    ucache *cache = ucache_new(&config);
    CacheValue *v1 = cache_value_new(1);
    CacheValue *v2 = cache_value_new(2);
    CacheValue *v3 = cache_value_new(3);
    CacheValue *v4 = cache_value_new(4);

    ucache_set(cache, "k1", 2, &v1->obj);
    ucache_set(cache, "k2", 2, &v2->obj);
    ucache_set(cache, "k3", 2, &v3->obj);
    ucache_set(cache, "k4", 2, &v4->obj);  // should evict k1

    ucache_stats stats;
    ucache_get_stats(cache, &stats);
    TEST_ASSERT_EQUAL_UINT64(3, stats.item_count);
    TEST_ASSERT_EQUAL_UINT64(1, stats.eviction_count);

    bool exists = true;
    ucache_exists(cache, "k1", 2, &exists);
    TEST_ASSERT_FALSE(exists);

    ucache_exists(cache, "k4", 2, &exists);
    TEST_ASSERT_TRUE(exists);

    uobject_release(&v1->obj);
    uobject_release(&v2->obj);
    uobject_release(&v3->obj);
    uobject_release(&v4->obj);
    ucache_free(cache);
}

static void test_ucache_eviction_lru_order(void) {
    ucache_config config = {
        .max_items = 2,
        .max_memory = 0,
        .thread_safe = false,
        .enable_stats = true,
    };
    ucache *cache = ucache_new(&config);
    CacheValue *v1 = cache_value_new(1);
    CacheValue *v2 = cache_value_new(2);
    CacheValue *v3 = cache_value_new(3);

    ucache_set(cache, "k1", 2, &v1->obj);
    ucache_set(cache, "k2", 2, &v2->obj);

    // Access k1 to make it most recently used
    ucache_get_retain(cache, "k1", 2, NULL);

    // Insert k3 should evict k2 (least recently used)
    ucache_set(cache, "k3", 2, &v3->obj);

    bool exists = true;
    ucache_exists(cache, "k1", 2, &exists);
    TEST_ASSERT_TRUE(exists);

    ucache_exists(cache, "k2", 2, &exists);
    TEST_ASSERT_FALSE(exists);

    ucache_exists(cache, "k3", 2, &exists);
    TEST_ASSERT_TRUE(exists);

    uobject_release(&v1->obj);
    uobject_release(&v2->obj);
    uobject_release(&v3->obj);
    ucache_free(cache);
}

static void test_ucache_eviction_by_memory(void) {
    ucache_config config = {
        .max_items = 0,
        .max_memory = 100,
        .thread_safe = false,
        .enable_stats = true,
    };
    ucache *cache = ucache_new(&config);

    // Each set uses key_len (2) + sizeof(CacheValue) bytes
    // sizeof(CacheValue) varies by platform, but we insert enough to trigger eviction

    for (int i = 0; i < 50; i++) {
        CacheValue *v = cache_value_new(i);
        char key[8];
        snprintf(key, sizeof(key), "k%d", i);
        ucache_set(cache, key, (uint32_t)strlen(key), &v->obj);
        uobject_release(&v->obj);
    }

    ucache_stats stats;
    ucache_get_stats(cache, &stats);
    TEST_ASSERT_TRUE(stats.item_count < 50);  // Some must have been evicted
    TEST_ASSERT_TRUE(stats.eviction_count > 0);
    TEST_ASSERT_TRUE(stats.current_memory <= 100 + sizeof(CacheValue) + 2); // Allow overshoot for single entry

    ucache_free(cache);
}

static void test_ucache_stats(void) {
    ucache_config config = {
        .max_items = 100,
        .max_memory = 0,
        .thread_safe = false,
        .enable_stats = true,
    };
    ucache *cache = ucache_new(&config);
    CacheValue *v = cache_value_new(1);

    ucache_set(cache, "key", 3, &v->obj);

    // Hit
    ucache_get_retain(cache, "key", 3, NULL);
    // Miss
    ucache_get_retain(cache, "nokey", 5, NULL);

    ucache_stats stats;
    ucache_get_stats(cache, &stats);
    TEST_ASSERT_EQUAL_UINT64(1, stats.hit_count);
    TEST_ASSERT_EQUAL_UINT64(1, stats.miss_count);
    TEST_ASSERT_EQUAL_UINT64(1, stats.item_count);
    TEST_ASSERT_EQUAL_UINT64(100, stats.max_items);

    uobject_release(&v->obj);
    ucache_free(cache);
}

static void test_ucache_null_params(void) {
    ucache_config cfg = basic_config();
    ucache *cache = ucache_new(&cfg);
    CacheValue *v = cache_value_new(1);

    TEST_ASSERT_EQUAL(UCACHE_ERR_NULL_CACHE, ucache_set(NULL, "k", 1, &v->obj));
    TEST_ASSERT_EQUAL(UCACHE_ERR_NULL_KEY, ucache_set(cache, NULL, 1, &v->obj));
    TEST_ASSERT_EQUAL(UCACHE_ERR_NULL_KEY, ucache_set(cache, "k", 0, &v->obj));
    TEST_ASSERT_EQUAL(UCACHE_ERR_NULL_VALUE, ucache_set(cache, "k", 1, NULL));

    TEST_ASSERT_EQUAL(UCACHE_ERR_NULL_CACHE, ucache_get_retain(NULL, "k", 1, NULL));
    TEST_ASSERT_EQUAL(UCACHE_ERR_NULL_KEY, ucache_get_retain(cache, NULL, 1, NULL));

    TEST_ASSERT_EQUAL(UCACHE_ERR_NULL_CACHE, ucache_delete(NULL, "k", 1));
    TEST_ASSERT_EQUAL(UCACHE_ERR_NULL_CACHE, ucache_clear(NULL));
    TEST_ASSERT_EQUAL(UCACHE_ERR_NULL_CACHE, ucache_free(NULL));
    TEST_ASSERT_EQUAL(UCACHE_ERR_NULL_CACHE, ucache_get_stats(NULL, NULL));

    uobject_release(&v->obj);
    ucache_free(cache);
}

static void test_ucache_result_string(void) {
    TEST_ASSERT_EQUAL_STRING("Success", ucache_result_string(UCACHE_OK));
    TEST_ASSERT_EQUAL_STRING("Cache is NULL", ucache_result_string(UCACHE_ERR_NULL_CACHE));
    TEST_ASSERT_EQUAL_STRING("Key is NULL or empty", ucache_result_string(UCACHE_ERR_NULL_KEY));
    TEST_ASSERT_EQUAL_STRING("Value is NULL", ucache_result_string(UCACHE_ERR_NULL_VALUE));
    TEST_ASSERT_EQUAL_STRING("Key not found", ucache_result_string(UCACHE_ERR_NOT_FOUND));
    TEST_ASSERT_EQUAL_STRING("Memory allocation failed", ucache_result_string(UCACHE_ERR_MEMORY));
    TEST_ASSERT_EQUAL_STRING("Unknown error", ucache_result_string((ucache_result)999));
}

static void test_ucache_thread_safe(void) {
    ucache_config config = {
        .max_items = 100,
        .max_memory = 0,
        .thread_safe = true,
        .enable_stats = true,
    };
    ucache *cache = ucache_new(&config);
    TEST_ASSERT_NOT_NULL(cache);

    CacheValue *v = cache_value_new(1);
    ucache_result r = ucache_set(cache, "key", 3, &v->obj);
    TEST_ASSERT_EQUAL(UCACHE_OK, r);

    uobject *result = NULL;
    r = ucache_get_retain(cache, "key", 3, &result);
    TEST_ASSERT_EQUAL(UCACHE_OK, r);
    TEST_ASSERT_NOT_NULL(result);

    uobject_release(result);
    uobject_release(&v->obj);
    ucache_free(cache);
}

static void test_ucache_free_null(void) {
    TEST_ASSERT_EQUAL(UCACHE_ERR_NULL_CACHE, ucache_free(NULL));
}

// ============================================================
// Edge case tests — eviction behavior
// ============================================================

// Eviction should decrement the value's refcount.
// When no one else holds a reference, the destructor must fire.
static void test_eviction_releases_refcount_and_destroys(void) {
    int destroyed = 0;
    ucache_config config = {
        .max_items = 1,
        .max_memory = 0,
        .thread_safe = false,
        .enable_stats = true,
    };
    ucache *cache = ucache_new(&config);

    TrackedValue *v1 = tracked_value_new(1, &destroyed);
    ucache_set(cache, "k1", 2, &v1->obj);
    // refcount: 1 (ours) + 1 (cache) = 2
    TEST_ASSERT_EQUAL_UINT32(2, uobject_refcount(&v1->obj));
    TEST_ASSERT_EQUAL_INT(0, destroyed);

    // Release our reference — only the cache holds it now (refcount = 1)
    uobject_release(&v1->obj);
    TEST_ASSERT_EQUAL_INT(0, destroyed);  // NOT destroyed yet

    // ucache_free releases cache's reference → refcount drops to 0 → destroyed
    ucache_free(cache);
    TEST_ASSERT_EQUAL_INT(1, destroyed);
}

// When eviction removes an entry and the caller still holds a reference,
// the object must survive (refcount drops but not to zero).
static void test_eviction_with_caller_held_reference(void) {
    int destroyed = 0;
    ucache_config config = {
        .max_items = 1,
        .max_memory = 0,
        .thread_safe = false,
        .enable_stats = true,
    };
    ucache *cache = ucache_new(&config);

    TrackedValue *v1 = tracked_value_new(1, &destroyed);
    ucache_set(cache, "k1", 2, &v1->obj);
    // refcount: 1 (ours) + 1 (cache) = 2
    TEST_ASSERT_EQUAL_UINT32(2, uobject_refcount(&v1->obj));

    // Insert k2 triggers eviction of k1
    TrackedValue *v2 = tracked_value_new(2, &(int){0});
    ucache_set(cache, "k2", 2, &v2->obj);

    // v1 is evicted from cache, but we still hold a reference → not destroyed
    TEST_ASSERT_EQUAL_INT(0, destroyed);
    TEST_ASSERT_EQUAL_UINT32(1, uobject_refcount(&v1->obj)); // only ours

    // Can still access v1
    TEST_ASSERT_EQUAL_INT(1, v1->id);

    uobject_release(&v1->obj); // now refcount → 0, destroyed
    TEST_ASSERT_EQUAL_INT(1, destroyed);

    uobject_release(&v2->obj);
    ucache_free(cache);
}

// max_items=1: only the most recent key survives
static void test_eviction_max_items_one(void) {
    ucache_config config = {
        .max_items = 1,
        .max_memory = 0,
        .thread_safe = false,
        .enable_stats = true,
    };
    ucache *cache = ucache_new(&config);
    CacheValue *v1 = cache_value_new(1);
    CacheValue *v2 = cache_value_new(2);
    CacheValue *v3 = cache_value_new(3);

    ucache_set(cache, "k1", 2, &v1->obj);

    ucache_stats stats;
    ucache_get_stats(cache, &stats);
    TEST_ASSERT_EQUAL_UINT64(1, stats.item_count);
    TEST_ASSERT_EQUAL_UINT64(0, stats.eviction_count);

    ucache_set(cache, "k2", 2, &v2->obj);
    ucache_get_stats(cache, &stats);
    TEST_ASSERT_EQUAL_UINT64(1, stats.item_count);
    TEST_ASSERT_EQUAL_UINT64(1, stats.eviction_count);

    bool exists = true;
    ucache_exists(cache, "k1", 2, &exists);
    TEST_ASSERT_FALSE(exists);
    ucache_exists(cache, "k2", 2, &exists);
    TEST_ASSERT_TRUE(exists);

    ucache_set(cache, "k3", 2, &v3->obj);
    ucache_get_stats(cache, &stats);
    TEST_ASSERT_EQUAL_UINT64(1, stats.item_count);
    TEST_ASSERT_EQUAL_UINT64(2, stats.eviction_count);

    ucache_exists(cache, "k2", 2, &exists);
    TEST_ASSERT_FALSE(exists);
    ucache_exists(cache, "k3", 2, &exists);
    TEST_ASSERT_TRUE(exists);

    uobject_release(&v1->obj);
    uobject_release(&v2->obj);
    uobject_release(&v3->obj);
    ucache_free(cache);
}

// Sequential fill-and-evict: verify the correct oldest item is removed at each step.
static void test_eviction_sequential_correctness(void) {
    ucache_config config = {
        .max_items = 3,
        .max_memory = 0,
        .thread_safe = false,
        .enable_stats = true,
    };
    ucache *cache = ucache_new(&config);

    // Batch 1: insert k1, k2, k3
    CacheValue *v1 = cache_value_new(1);
    CacheValue *v2 = cache_value_new(2);
    CacheValue *v3 = cache_value_new(3);
    ucache_set(cache, "k1", 2, &v1->obj);
    ucache_set(cache, "k2", 2, &v2->obj);
    ucache_set(cache, "k3", 2, &v3->obj);

    // Insert k4 → evicts k1
    CacheValue *v4 = cache_value_new(4);
    ucache_set(cache, "k4", 2, &v4->obj);

    bool exists = true;
    ucache_exists(cache, "k1", 2, &exists);
    TEST_ASSERT_FALSE(exists);
    ucache_exists(cache, "k2", 2, &exists);
    TEST_ASSERT_TRUE(exists);
    ucache_exists(cache, "k3", 2, &exists);
    TEST_ASSERT_TRUE(exists);
    ucache_exists(cache, "k4", 2, &exists);
    TEST_ASSERT_TRUE(exists);

    // Insert k5 → evicts k2
    CacheValue *v5 = cache_value_new(5);
    ucache_set(cache, "k5", 2, &v5->obj);

    ucache_exists(cache, "k2", 2, &exists);
    TEST_ASSERT_FALSE(exists);
    ucache_exists(cache, "k3", 2, &exists);
    TEST_ASSERT_TRUE(exists);
    ucache_exists(cache, "k4", 2, &exists);
    TEST_ASSERT_TRUE(exists);
    ucache_exists(cache, "k5", 2, &exists);
    TEST_ASSERT_TRUE(exists);

    // Insert k6 → evicts k3
    CacheValue *v6 = cache_value_new(6);
    ucache_set(cache, "k6", 2, &v6->obj);

    ucache_exists(cache, "k3", 2, &exists);
    TEST_ASSERT_FALSE(exists);
    ucache_exists(cache, "k4", 2, &exists);
    TEST_ASSERT_TRUE(exists);
    ucache_exists(cache, "k5", 2, &exists);
    TEST_ASSERT_TRUE(exists);
    ucache_exists(cache, "k6", 2, &exists);
    TEST_ASSERT_TRUE(exists);

    ucache_stats stats;
    ucache_get_stats(cache, &stats);
    TEST_ASSERT_EQUAL_UINT64(3, stats.item_count);
    TEST_ASSERT_EQUAL_UINT64(3, stats.eviction_count);

    uobject_release(&v1->obj);
    uobject_release(&v2->obj);
    uobject_release(&v3->obj);
    uobject_release(&v4->obj);
    uobject_release(&v5->obj);
    uobject_release(&v6->obj);
    ucache_free(cache);
}

// Updating an existing key should NOT change item_count or trigger eviction.
static void test_update_does_not_trigger_eviction(void) {
    ucache_config config = {
        .max_items = 2,
        .max_memory = 0,
        .thread_safe = false,
        .enable_stats = true,
    };
    ucache *cache = ucache_new(&config);
    CacheValue *v1 = cache_value_new(1);
    CacheValue *v2 = cache_value_new(2);
    CacheValue *v2_new = cache_value_new(22);

    ucache_set(cache, "k1", 2, &v1->obj);
    ucache_set(cache, "k2", 2, &v2->obj);

    ucache_stats stats;
    ucache_get_stats(cache, &stats);
    TEST_ASSERT_EQUAL_UINT64(2, stats.item_count);
    TEST_ASSERT_EQUAL_UINT64(0, stats.eviction_count);

    // Update k2's value — should NOT evict anything
    ucache_set(cache, "k2", 2, &v2_new->obj);

    ucache_get_stats(cache, &stats);
    TEST_ASSERT_EQUAL_UINT64(2, stats.item_count);      // unchanged
    TEST_ASSERT_EQUAL_UINT64(0, stats.eviction_count);  // no eviction

    // Both keys still exist
    bool exists = false;
    ucache_exists(cache, "k1", 2, &exists);
    TEST_ASSERT_TRUE(exists);
    ucache_exists(cache, "k2", 2, &exists);
    TEST_ASSERT_TRUE(exists);

    // k2's value was replaced
    uobject *result = NULL;
    ucache_get_retain(cache, "k2", 2, &result);
    TEST_ASSERT_EQUAL_PTR(&v2_new->obj, result);
    uobject_release(result);

    uobject_release(&v1->obj);
    uobject_release(&v2->obj);
    uobject_release(&v2_new->obj);
    ucache_free(cache);
}

// Updating an existing key should move it to the front of the LRU list.
static void test_update_moves_to_front_lru(void) {
    ucache_config config = {
        .max_items = 2,
        .max_memory = 0,
        .thread_safe = false,
        .enable_stats = true,
    };
    ucache *cache = ucache_new(&config);
    CacheValue *v1 = cache_value_new(1);
    CacheValue *v2 = cache_value_new(2);
    CacheValue *v1_new = cache_value_new(11);
    CacheValue *v3 = cache_value_new(3);

    ucache_set(cache, "k1", 2, &v1->obj);
    ucache_set(cache, "k2", 2, &v2->obj);

    // Update k1 — should move it to MRU position
    ucache_set(cache, "k1", 2, &v1_new->obj);

    // Insert k3 — should evict k2 (now the LRU), not k1
    ucache_set(cache, "k3", 2, &v3->obj);

    bool exists = true;
    ucache_exists(cache, "k1", 2, &exists);
    TEST_ASSERT_TRUE(exists);

    ucache_exists(cache, "k2", 2, &exists);
    TEST_ASSERT_FALSE(exists);  // k2 evicted, not k1

    ucache_exists(cache, "k3", 2, &exists);
    TEST_ASSERT_TRUE(exists);

    uobject_release(&v1->obj);
    uobject_release(&v2->obj);
    uobject_release(&v1_new->obj);
    uobject_release(&v3->obj);
    ucache_free(cache);
}

// After clearing, the cache should work correctly for fresh inserts and eviction.
static void test_clear_then_reuse(void) {
    ucache_config config = {
        .max_items = 2,
        .max_memory = 0,
        .thread_safe = false,
        .enable_stats = true,
    };
    ucache *cache = ucache_new(&config);
    CacheValue *v1 = cache_value_new(1);
    CacheValue *v2 = cache_value_new(2);

    ucache_set(cache, "k1", 2, &v1->obj);
    ucache_set(cache, "k2", 2, &v2->obj);

    ucache_clear(cache);

    ucache_stats stats;
    ucache_get_stats(cache, &stats);
    TEST_ASSERT_EQUAL_UINT64(0, stats.item_count);
    TEST_ASSERT_EQUAL_UINT64(0, stats.current_memory);

    // Re-insert after clear
    CacheValue *v3 = cache_value_new(3);
    CacheValue *v4 = cache_value_new(4);
    CacheValue *v5 = cache_value_new(5);

    ucache_set(cache, "k3", 2, &v3->obj);
    ucache_set(cache, "k4", 2, &v4->obj);

    bool exists = false;
    ucache_exists(cache, "k3", 2, &exists);
    TEST_ASSERT_TRUE(exists);
    ucache_exists(cache, "k4", 2, &exists);
    TEST_ASSERT_TRUE(exists);

    // Insert k5 → should evict k3 (oldest)
    ucache_set(cache, "k5", 2, &v5->obj);

    ucache_exists(cache, "k3", 2, &exists);
    TEST_ASSERT_FALSE(exists);
    ucache_exists(cache, "k4", 2, &exists);
    TEST_ASSERT_TRUE(exists);
    ucache_exists(cache, "k5", 2, &exists);
    TEST_ASSERT_TRUE(exists);

    ucache_get_stats(cache, &stats);
    TEST_ASSERT_EQUAL_UINT64(2, stats.item_count);

    uobject_release(&v1->obj);
    uobject_release(&v2->obj);
    uobject_release(&v3->obj);
    uobject_release(&v4->obj);
    uobject_release(&v5->obj);
    ucache_free(cache);
}

// When both max_items and max_memory are set, either limit can trigger eviction.
static void test_combined_max_items_and_memory(void) {
    ucache_config config = {
        .max_items = 100,
        .max_memory = 100,
        .thread_safe = false,
        .enable_stats = true,
    };
    ucache *cache = ucache_new(&config);

    // Insert enough items to trigger memory-based eviction even though
    // max_items is far from reached
    for (int i = 0; i < 50; i++) {
        CacheValue *v = cache_value_new(i);
        char key[8];
        snprintf(key, sizeof(key), "k%d", i);
        ucache_set(cache, key, (uint32_t)strlen(key), &v->obj);
        uobject_release(&v->obj);
    }

    ucache_stats stats;
    ucache_get_stats(cache, &stats);
    TEST_ASSERT_TRUE(stats.item_count < 50);       // memory eviction kicked in
    TEST_ASSERT_TRUE(stats.eviction_count > 0);
    TEST_ASSERT_TRUE(stats.current_memory <= 100 + sizeof(CacheValue) + 2);

    ucache_free(cache);
}

// Inserting one item that exceeds max_memory should still succeed (best-effort).
// The cache should self-correct on subsequent inserts.
static void test_single_entry_exceeds_memory_limit(void) {
    ucache_config config = {
        .max_items = 0,
        .max_memory = 10,  // very small
        .thread_safe = false,
        .enable_stats = true,
    };
    ucache *cache = ucache_new(&config);

    // sizeof(CacheValue) is much larger than 10, but insert should succeed
    CacheValue *v1 = cache_value_new(1);
    ucache_result r = ucache_set(cache, "k1", 2, &v1->obj);
    TEST_ASSERT_EQUAL(UCACHE_OK, r);

    ucache_stats stats;
    ucache_get_stats(cache, &stats);
    TEST_ASSERT_EQUAL_UINT64(1, stats.item_count);
    TEST_ASSERT_TRUE(stats.current_memory > 10);  // overshoot is expected

    // Second insert should evict k1
    CacheValue *v2 = cache_value_new(2);
    ucache_set(cache, "k2", 2, &v2->obj);

    ucache_get_stats(cache, &stats);
    TEST_ASSERT_EQUAL_UINT64(1, stats.item_count);
    TEST_ASSERT_EQUAL_UINT64(1, stats.eviction_count);

    bool exists = true;
    ucache_exists(cache, "k1", 2, &exists);
    TEST_ASSERT_FALSE(exists);
    ucache_exists(cache, "k2", 2, &exists);
    TEST_ASSERT_TRUE(exists);

    uobject_release(&v1->obj);
    uobject_release(&v2->obj);
    ucache_free(cache);
}

// Delete an entry, then re-insert the same key — counts should be consistent.
static void test_delete_then_reinsert(void) {
    ucache_config config = {
        .max_items = 2,
        .max_memory = 0,
        .thread_safe = false,
        .enable_stats = true,
    };
    ucache *cache = ucache_new(&config);
    CacheValue *v1 = cache_value_new(1);
    CacheValue *v2 = cache_value_new(2);
    CacheValue *v3 = cache_value_new(3);
    CacheValue *v1_new = cache_value_new(11);

    ucache_set(cache, "k1", 2, &v1->obj);
    ucache_set(cache, "k2", 2, &v2->obj);

    // Delete k1
    ucache_delete(cache, "k1", 2);

    ucache_stats stats;
    ucache_get_stats(cache, &stats);
    TEST_ASSERT_EQUAL_UINT64(1, stats.item_count);

    // Re-insert k1 with a new value
    ucache_set(cache, "k1", 2, &v1_new->obj);

    ucache_get_stats(cache, &stats);
    TEST_ASSERT_EQUAL_UINT64(2, stats.item_count);
    TEST_ASSERT_EQUAL_UINT64(0, stats.eviction_count);  // no eviction needed

    // Now fill to 3 → should evict k2 (LRU), not k1
    ucache_set(cache, "k3", 2, &v3->obj);

    bool exists = true;
    ucache_exists(cache, "k2", 2, &exists);
    TEST_ASSERT_FALSE(exists);
    ucache_exists(cache, "k1", 2, &exists);
    TEST_ASSERT_TRUE(exists);
    ucache_exists(cache, "k3", 2, &exists);
    TEST_ASSERT_TRUE(exists);

    uobject_release(&v1->obj);
    uobject_release(&v2->obj);
    uobject_release(&v3->obj);
    uobject_release(&v1_new->obj);
    ucache_free(cache);
}

// ucache_get_retain updates LRU position — accessed items should be evicted last.
static void test_get_updates_lru_order(void) {
    ucache_config config = {
        .max_items = 3,
        .max_memory = 0,
        .thread_safe = false,
        .enable_stats = true,
    };
    ucache *cache = ucache_new(&config);
    CacheValue *v1 = cache_value_new(1);
    CacheValue *v2 = cache_value_new(2);
    CacheValue *v3 = cache_value_new(3);
    CacheValue *v4 = cache_value_new(4);
    CacheValue *v5 = cache_value_new(5);

    ucache_set(cache, "k1", 2, &v1->obj);  // MRU: k1
    ucache_set(cache, "k2", 2, &v2->obj);  // MRU: k2, k1
    ucache_set(cache, "k3", 2, &v3->obj);  // MRU: k3, k2, k1

    // Access k1 via get_retain → moves to front: MRU: k1, k3, k2
    uobject *result = NULL;
    ucache_get_retain(cache, "k1", 2, &result);
    TEST_ASSERT_NOT_NULL(result);
    uobject_release(result);

    // Insert k4 → evicts k2 (LRU)
    ucache_set(cache, "k4", 2, &v4->obj);

    bool exists = true;
    ucache_exists(cache, "k2", 2, &exists);
    TEST_ASSERT_FALSE(exists);  // evicted
    ucache_exists(cache, "k1", 2, &exists);
    TEST_ASSERT_TRUE(exists);   // was accessed, survived
    ucache_exists(cache, "k3", 2, &exists);
    TEST_ASSERT_TRUE(exists);

    // Access k3 via exists (doesn't update LRU — exists is non-promoting)
    ucache_exists(cache, "k3", 2, &exists);

    // Insert k5 → evicts k3 (LRU: k3 is older than k1 and k4)
    ucache_set(cache, "k5", 2, &v5->obj);

    ucache_exists(cache, "k3", 2, &exists);
    TEST_ASSERT_FALSE(exists);  // evicted
    ucache_exists(cache, "k1", 2, &exists);
    TEST_ASSERT_TRUE(exists);
    ucache_exists(cache, "k4", 2, &exists);
    TEST_ASSERT_TRUE(exists);
    ucache_exists(cache, "k5", 2, &exists);
    TEST_ASSERT_TRUE(exists);

    uobject_release(&v1->obj);
    uobject_release(&v2->obj);
    uobject_release(&v3->obj);
    uobject_release(&v4->obj);
    uobject_release(&v5->obj);
    ucache_free(cache);
}

// Memory-based eviction may need to evict multiple entries in a single insert.
static void test_memory_eviction_multiple_entries(void) {
    // Set memory limit so small that inserting one item requires
    // evicting all previous entries
    ucache_config config = {
        .max_items = 0,
        .max_memory = sizeof(CacheValue) + 4,  // room for ~1 entry
        .thread_safe = false,
        .enable_stats = true,
    };
    ucache *cache = ucache_new(&config);

    CacheValue *v1 = cache_value_new(1);
    CacheValue *v2 = cache_value_new(2);
    CacheValue *v3 = cache_value_new(3);

    ucache_set(cache, "k1", 2, &v1->obj);
    ucache_set(cache, "k2", 2, &v2->obj);

    // Both v1 and v2 may have been evicted by now depending on sizes.
    // Insert v3 → should evict until fits
    ucache_set(cache, "k3", 2, &v3->obj);

    ucache_stats stats;
    ucache_get_stats(cache, &stats);
    TEST_ASSERT_EQUAL_UINT64(1, stats.item_count);  // only k3 remains
    TEST_ASSERT_TRUE(stats.eviction_count >= 2);     // at least k1 and k2 evicted

    bool exists = true;
    ucache_exists(cache, "k3", 2, &exists);
    TEST_ASSERT_TRUE(exists);

    uobject_release(&v1->obj);
    uobject_release(&v2->obj);
    uobject_release(&v3->obj);
    ucache_free(cache);
}

// Stats should not count when enable_stats is false
static void test_stats_disabled(void) {
    ucache_config config = {
        .max_items = 100,
        .max_memory = 0,
        .thread_safe = false,
        .enable_stats = false,
    };
    ucache *cache = ucache_new(&config);
    CacheValue *v = cache_value_new(1);

    ucache_set(cache, "key", 3, &v->obj);
    ucache_get_retain(cache, "key", 3, NULL);   // hit
    ucache_get_retain(cache, "nokey", 5, NULL); // miss

    ucache_stats stats;
    ucache_get_stats(cache, &stats);
    TEST_ASSERT_EQUAL_UINT64(0, stats.hit_count);
    TEST_ASSERT_EQUAL_UINT64(0, stats.miss_count);
    TEST_ASSERT_EQUAL_UINT64(0, stats.eviction_count);
    TEST_ASSERT_EQUAL_UINT64(1, stats.item_count);  // item_count still tracked

    uobject_release(&v->obj);
    ucache_free(cache);
}

// Updating a key changes its memory footprint; current_memory should adjust.
static void test_update_changes_memory_tracking(void) {
    ucache_config config = {
        .max_items = 100,
        .max_memory = 0,
        .thread_safe = false,
        .enable_stats = true,
    };
    ucache *cache = ucache_new(&config);
    CacheValue *v1 = cache_value_new(1);

    ucache_set(cache, "k1", 2, &v1->obj);

    ucache_stats stats;
    ucache_get_stats(cache, &stats);
    uint64_t mem_before = stats.current_memory;

    // Update same key with same value — memory should stay the same
    ucache_set(cache, "k1", 2, &v1->obj);
    ucache_get_stats(cache, &stats);
    TEST_ASSERT_EQUAL_UINT64(mem_before, stats.current_memory);

    uobject_release(&v1->obj);
    ucache_free(cache);
}

// Delete should decrement current_memory by the entry's total size.
static void test_delete_updates_memory(void) {
    ucache_config config = {
        .max_items = 100,
        .max_memory = 0,
        .thread_safe = false,
        .enable_stats = true,
    };
    ucache *cache = ucache_new(&config);
    CacheValue *v1 = cache_value_new(1);
    CacheValue *v2 = cache_value_new(2);

    ucache_set(cache, "k1", 2, &v1->obj);
    ucache_set(cache, "k2", 2, &v2->obj);

    ucache_stats stats;
    ucache_get_stats(cache, &stats);
    uint64_t mem_with_both = stats.current_memory;

    ucache_delete(cache, "k1", 2);

    ucache_get_stats(cache, &stats);
    TEST_ASSERT_TRUE(stats.current_memory < mem_with_both);
    TEST_ASSERT_EQUAL_UINT64(mem_with_both - (2 + sizeof(CacheValue)),
                              stats.current_memory);

    uobject_release(&v1->obj);
    uobject_release(&v2->obj);
    ucache_free(cache);
}

// ============================================================
// Test runner
// ============================================================

void run_ucache_tests(void) {
    printf("\n");
    printf("========================================\n");
    printf("  ucache Tests\n");
    printf("========================================\n");

    RUN_TEST(test_ucache_new_basic);
    RUN_TEST(test_ucache_new_null_config);
    RUN_TEST(test_ucache_new_no_limits);
    RUN_TEST(test_ucache_new_custom_capacity);
    RUN_TEST(test_ucache_set_get_basic);
    RUN_TEST(test_ucache_set_update_existing);
    RUN_TEST(test_ucache_get_not_found);
    RUN_TEST(test_ucache_get_retain_refcount);
    RUN_TEST(test_ucache_exists);
    RUN_TEST(test_ucache_delete);
    RUN_TEST(test_ucache_delete_not_found);
    RUN_TEST(test_ucache_clear);
    RUN_TEST(test_ucache_eviction_by_items);
    RUN_TEST(test_ucache_eviction_lru_order);
    RUN_TEST(test_ucache_eviction_by_memory);
    RUN_TEST(test_ucache_stats);
    RUN_TEST(test_ucache_null_params);
    RUN_TEST(test_ucache_result_string);
    RUN_TEST(test_ucache_thread_safe);
    RUN_TEST(test_ucache_free_null);

    // Edge cases — eviction behavior
    RUN_TEST(test_eviction_releases_refcount_and_destroys);
    RUN_TEST(test_eviction_with_caller_held_reference);
    RUN_TEST(test_eviction_max_items_one);
    RUN_TEST(test_eviction_sequential_correctness);
    RUN_TEST(test_update_does_not_trigger_eviction);
    RUN_TEST(test_update_moves_to_front_lru);
    RUN_TEST(test_clear_then_reuse);
    RUN_TEST(test_combined_max_items_and_memory);
    RUN_TEST(test_single_entry_exceeds_memory_limit);
    RUN_TEST(test_delete_then_reinsert);
    RUN_TEST(test_get_updates_lru_order);
    RUN_TEST(test_memory_eviction_multiple_entries);
    RUN_TEST(test_stats_disabled);
    RUN_TEST(test_update_changes_memory_tracking);
    RUN_TEST(test_delete_updates_memory);
}
