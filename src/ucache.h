//
//  ucache.h
//
//  Created by kejinlu on 2026-04-19.
//
//  LRU cache with uobject-managed values.
//

#ifndef ucache_h
#define ucache_h

#include "uobject.h"
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    UCACHE_OK = 0,
    UCACHE_ERR_NULL_CACHE,
    UCACHE_ERR_NULL_KEY,
    UCACHE_ERR_NULL_VALUE,
    UCACHE_ERR_NOT_FOUND,
    UCACHE_ERR_MEMORY
} ucache_result;

typedef struct ucache ucache;

typedef struct {
    uint64_t max_items;         // Max number of entries (0 = unlimited)
    uint64_t max_memory;        // Max memory usage in bytes (0 = unlimited).
                                // Best-effort limit: a single entry larger than
                                // max_memory may cause temporary overshoot;
                                // the cache will self-correct on subsequent inserts.
    uint32_t initial_capacity;  // Initial hash table capacity
    bool thread_safe;           // Enable mutex-based thread safety
    bool enable_stats;          // Enable hit/miss/eviction statistics
} ucache_config;

ucache *ucache_new(const ucache_config *config);
ucache_result ucache_free(ucache *cache);

// Set value in cache.
// Cache calls uobject_retain internally; caller keeps their own reference.
// @value: pointer to a uobject-based struct (uobject must be embedded member)
ucache_result ucache_set(ucache *cache,
                            const void *key, uint32_t key_len,
                            uobject *value);

// Get value with refcount incremented (via uobject_retain).
// Caller MUST call uobject_release when done.
// Value remains valid even if evicted from cache.
ucache_result ucache_get_retain(ucache *cache,
                                     const void *key, uint32_t key_len,
                                     uobject **value);

ucache_result ucache_exists(ucache *cache,
                               const void *key, uint32_t key_len,
                               bool *exists);

ucache_result ucache_delete(ucache *cache,
                               const void *key, uint32_t key_len);

ucache_result ucache_clear(ucache *cache);

typedef struct {
    uint64_t item_count;
    uint64_t max_items;
    uint64_t current_memory;    // Current memory usage in bytes
    uint64_t max_memory;
    uint64_t hit_count;
    uint64_t miss_count;
    uint64_t eviction_count;
} ucache_stats;

ucache_result ucache_get_stats(ucache *cache, ucache_stats *stats);
const char *ucache_result_string(ucache_result result);

#endif /* ucache_h */
