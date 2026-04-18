//
//  ucache.c
//
//  Created by kejinlu on 2026-04-19.
//
//  LRU cache with uobject-managed values.
//

#include "ucache.h"
#include "uhash.h"
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

// ------------------------------------------
// Constants
// ------------------------------------------
#define LOAD_FACTOR_THRESHOLD 0.75
#define MIN_CAPACITY 16
#define HASH_SEED 0x9747b28c

// ------------------------------------------
// Platform mutex abstraction
// ------------------------------------------
#ifdef _WIN32
typedef SRWLOCK ucache_mutex;
#define MUTEX_INIT(m)    (InitializeSRWLock(&(m)), true)
#define MUTEX_DESTROY(m) ((void)0)
#define MUTEX_LOCK(m)    AcquireSRWLockExclusive(&(m))
#define MUTEX_UNLOCK(m)  ReleaseSRWLockExclusive(&(m))
#else
typedef pthread_mutex_t ucache_mutex;
#define MUTEX_INIT(m)    (pthread_mutex_init(&(m), NULL) == 0)
#define MUTEX_DESTROY(m) pthread_mutex_destroy(&(m))
#define MUTEX_LOCK(m)    pthread_mutex_lock(&(m))
#define MUTEX_UNLOCK(m)  pthread_mutex_unlock(&(m))
#endif

// ------------------------------------------
// Internal structures
// ------------------------------------------

typedef struct ucache_entry {
    void *key;
    uobject *value;                // uobject-managed pointer
    uint64_t value_size;           // Memory footprint of value
    uint32_t key_len;
    uint32_t hash;
    struct ucache_entry *hash_next; // Hash chain (singly linked)
    struct ucache_entry *prev;      // Access list (doubly linked)
    struct ucache_entry *next;
} ucache_entry;

struct ucache {
    ucache_entry **buckets;
    uint32_t capacity;
    uint64_t item_count;

    ucache_entry *head;             // Most recently used
    ucache_entry *tail;             // Least recently used

    uint64_t max_items;
    uint64_t max_memory;            // Max memory usage, 0 = unlimited
    uint64_t current_memory;        // Current memory usage

    bool enable_stats;
    uint64_t hit_count;
    uint64_t miss_count;
    uint64_t eviction_count;

    bool thread_safe;
    ucache_mutex mutex;
};

// ------------------------------------------
// Statistics macros
// ------------------------------------------
#define STATS_INC(cache, field) \
    do { if ((cache)->enable_stats) (cache)->field++; } while(0)

// ------------------------------------------
// Helper functions
// ------------------------------------------

static inline bool is_power_of_2(uint32_t n) {
    return n > 0 && (n & (n - 1)) == 0;
}

static inline uint32_t round_up_to_power_of_2(uint32_t n) {
    if (n == 0) return 1;
    if (is_power_of_2(n)) return n;
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    return n + 1;
}

static inline uint32_t hash_bytes(const void *key, uint32_t len) {
    return murmur3_32(key, len, HASH_SEED);
}

static inline uint32_t bucket_index(uint32_t hash, uint32_t capacity) {
    return hash & (capacity - 1);
}

static inline void cache_lock(ucache *cache) {
    if (cache->thread_safe) {
        MUTEX_LOCK(cache->mutex);
    }
}

static inline void cache_unlock(ucache *cache) {
    if (cache->thread_safe) {
        MUTEX_UNLOCK(cache->mutex);
    }
}

// ------------------------------------------
// Access list operations
// ------------------------------------------

static void list_remove(ucache *cache, ucache_entry *entry) {
    if (entry->prev) {
        entry->prev->next = entry->next;
    } else {
        cache->head = entry->next;
    }

    if (entry->next) {
        entry->next->prev = entry->prev;
    } else {
        cache->tail = entry->prev;
    }

    entry->prev = NULL;
    entry->next = NULL;
}

static void list_push_front(ucache *cache, ucache_entry *entry) {
    entry->prev = NULL;
    entry->next = cache->head;

    if (cache->head) {
        cache->head->prev = entry;
    } else {
        cache->tail = entry;
    }

    cache->head = entry;
}

static void list_move_to_front(ucache *cache, ucache_entry *entry) {
    if (cache->head == entry) {
        return;
    }
    list_remove(cache, entry);
    list_push_front(cache, entry);
}

// ------------------------------------------
// Hash table operations
// ------------------------------------------

static ucache_entry *hash_find(ucache *cache, const void *key,
                                  uint32_t key_len, uint32_t hash,
                                  ucache_entry ***prev_ptr) {
    uint32_t idx = bucket_index(hash, cache->capacity);
    ucache_entry **prev = &cache->buckets[idx];
    ucache_entry *entry = *prev;

    while (entry) {
        if (entry->hash == hash &&
            entry->key_len == key_len &&
            memcmp(entry->key, key, key_len) == 0) {
            if (prev_ptr) *prev_ptr = prev;
            return entry;
        }
        prev = &entry->hash_next;
        entry = entry->hash_next;
    }

    return NULL;
}

static void hash_insert(ucache *cache, ucache_entry *entry) {
    uint32_t idx = bucket_index(entry->hash, cache->capacity);
    entry->hash_next = cache->buckets[idx];
    cache->buckets[idx] = entry;
    cache->item_count++;
}

static void hash_remove(ucache *cache, ucache_entry *entry,
                        ucache_entry **prev) {
    *prev = entry->hash_next;
    entry->hash_next = NULL;
    cache->item_count--;
}

// ------------------------------------------
// Entry management
// ------------------------------------------

static inline uint64_t entry_total_size(const ucache_entry *entry) {
    return entry->key_len + entry->value_size;
}

static ucache_entry *entry_create(const void *key, uint32_t key_len,
                                    uobject *value, uint32_t hash) {
    ucache_entry *entry = malloc(sizeof(ucache_entry));
    if (!entry) return NULL;

    entry->key = malloc(key_len);
    if (!entry->key) {
        free(entry);
        return NULL;
    }

    memcpy(entry->key, key, key_len);
    entry->value = uobject_retain(value); // Retain and store pointer
    entry->value_size = uobject_memory_size(value);
    entry->key_len = key_len;
    entry->hash = hash;
    entry->hash_next = NULL;
    entry->prev = NULL;
    entry->next = NULL;

    return entry;
}

static void entry_free(ucache_entry *entry) {
    if (entry) {
        free(entry->key);
        uobject_release(entry->value);  // Release uobject reference
        free(entry);
    }
}

// ------------------------------------------
// Eviction
// ------------------------------------------

static void evict_oldest(ucache *cache) {
    ucache_entry *victim = cache->tail;
    if (!victim) return;

    list_remove(cache, victim);

    ucache_entry **prev = NULL;
    hash_find(cache, victim->key, victim->key_len, victim->hash, &prev);
    if (prev) {
        hash_remove(cache, victim, prev);
    }

    cache->current_memory -= entry_total_size(victim);
    STATS_INC(cache, eviction_count);

    entry_free(victim);
}

static bool needs_eviction(ucache *cache, bool is_new_entry, uint64_t pending_size) {
    if (!is_new_entry) return false;
    if (cache->max_items > 0 && cache->item_count >= cache->max_items) {
        return true;
    }
    if (cache->max_memory > 0 && cache->current_memory + pending_size > cache->max_memory) {
        return true;
    }
    return false;
}

static void evict_until_fit(ucache *cache, bool is_new_entry, uint64_t pending_size) {
    while (needs_eviction(cache, is_new_entry, pending_size) && cache->tail) {
        evict_oldest(cache);
    }
}

// ------------------------------------------
// Hash table resizing
// ------------------------------------------

static bool should_resize(ucache *cache) {
    return (double)cache->item_count / cache->capacity > LOAD_FACTOR_THRESHOLD;
}

static ucache_result resize_table(ucache *cache) {
    uint32_t new_capacity = cache->capacity * 2;
    ucache_entry **new_buckets = calloc(new_capacity, sizeof(ucache_entry *));
    if (!new_buckets) {
        return UCACHE_ERR_MEMORY;
    }

    for (uint32_t i = 0; i < cache->capacity; i++) {
        ucache_entry *entry = cache->buckets[i];
        while (entry) {
            ucache_entry *next = entry->hash_next;
            uint32_t new_idx = bucket_index(entry->hash, new_capacity);
            entry->hash_next = new_buckets[new_idx];
            new_buckets[new_idx] = entry;
            entry = next;
        }
    }

    free(cache->buckets);
    cache->buckets = new_buckets;
    cache->capacity = new_capacity;

    return UCACHE_OK;
}

// ------------------------------------------
// Public API
// ------------------------------------------

ucache *ucache_new(const ucache_config *config) {
    if (!config || (config->max_items == 0 && config->max_memory == 0)) {
        return NULL;
    }

    ucache *cache = calloc(1, sizeof(ucache));
    if (!cache) return NULL;

    uint32_t capacity = config->initial_capacity;
    if (capacity < MIN_CAPACITY) {
        capacity = MIN_CAPACITY;
    } else {
        capacity = round_up_to_power_of_2(capacity);
    }

    cache->buckets = calloc(capacity, sizeof(ucache_entry *));
    if (!cache->buckets) {
        free(cache);
        return NULL;
    }

    cache->capacity = capacity;
    cache->max_items = config->max_items;
    cache->max_memory = config->max_memory;
    cache->thread_safe = config->thread_safe;
    cache->enable_stats = config->enable_stats;

    if (config->thread_safe) {
        if (!MUTEX_INIT(cache->mutex)) {
            free(cache->buckets);
            free(cache);
            return NULL;
        }
    }

    return cache;
}

ucache_result ucache_free(ucache *cache) {
    if (!cache) return UCACHE_ERR_NULL_CACHE;

    ucache_entry *entry = cache->head;
    while (entry) {
        ucache_entry *next = entry->next;
        entry_free(entry);
        entry = next;
    }

    free(cache->buckets);

    if (cache->thread_safe) {
        MUTEX_DESTROY(cache->mutex);
    }

    free(cache);
    return UCACHE_OK;
}

ucache_result ucache_set(ucache *cache,
                            const void *key, uint32_t key_len,
                            uobject *value) {
    if (!cache) return UCACHE_ERR_NULL_CACHE;
    if (!key || key_len == 0) return UCACHE_ERR_NULL_KEY;
    if (!value) return UCACHE_ERR_NULL_VALUE;

    cache_lock(cache);

    uint32_t hash = hash_bytes(key, key_len);
    ucache_entry **prev_ptr = NULL;
    ucache_entry *existing = hash_find(cache, key, key_len, hash, &prev_ptr);

    if (existing) {
        // Update existing entry
        cache->current_memory -= entry_total_size(existing);
        uobject_release(existing->value);
        existing->value = uobject_retain(value);
        existing->value_size = uobject_memory_size(value);
        cache->current_memory += entry_total_size(existing);
        list_move_to_front(cache, existing);
    } else {
        // Create new entry
        uint64_t pending_size = key_len + uobject_memory_size(value);
        evict_until_fit(cache, true, pending_size);

        ucache_entry *entry = entry_create(key, key_len, value, hash);
        if (!entry) {
            cache_unlock(cache);
            return UCACHE_ERR_MEMORY;
        }

        cache->current_memory += entry_total_size(entry);
        hash_insert(cache, entry);
        list_push_front(cache, entry);

        if (should_resize(cache)) {
            resize_table(cache);
        }
    }

    cache_unlock(cache);
    return UCACHE_OK;
}

ucache_result ucache_get_retain(ucache *cache,
                                     const void *key, uint32_t key_len,
                                     uobject **value) {
    if (!cache) return UCACHE_ERR_NULL_CACHE;
    if (!key || key_len == 0) return UCACHE_ERR_NULL_KEY;

    cache_lock(cache);

    uint32_t hash = hash_bytes(key, key_len);
    ucache_entry *entry = hash_find(cache, key, key_len, hash, NULL);

    if (entry) {
        if (value) {
            *value = uobject_retain(entry->value);  // Return with incremented refcount
        }
        list_move_to_front(cache, entry);
        STATS_INC(cache, hit_count);
        cache_unlock(cache);
        return UCACHE_OK;
    }

    STATS_INC(cache, miss_count);
    cache_unlock(cache);

    if (value) *value = NULL;
    return UCACHE_ERR_NOT_FOUND;
}

ucache_result ucache_exists(ucache *cache,
                               const void *key, uint32_t key_len,
                               bool *exists) {
    if (!cache) return UCACHE_ERR_NULL_CACHE;
    if (!key || key_len == 0) return UCACHE_ERR_NULL_KEY;
    if (!exists) return UCACHE_OK;

    cache_lock(cache);

    uint32_t hash = hash_bytes(key, key_len);
    ucache_entry *entry = hash_find(cache, key, key_len, hash, NULL);

    *exists = (entry != NULL);

    cache_unlock(cache);
    return UCACHE_OK;
}

ucache_result ucache_delete(ucache *cache,
                               const void *key, uint32_t key_len) {
    if (!cache) return UCACHE_ERR_NULL_CACHE;
    if (!key || key_len == 0) return UCACHE_ERR_NULL_KEY;

    cache_lock(cache);

    uint32_t hash = hash_bytes(key, key_len);
    ucache_entry **prev_ptr = NULL;
    ucache_entry *entry = hash_find(cache, key, key_len, hash, &prev_ptr);

    if (!entry) {
        cache_unlock(cache);
        return UCACHE_ERR_NOT_FOUND;
    }

    list_remove(cache, entry);
    hash_remove(cache, entry, prev_ptr);

    cache->current_memory -= entry_total_size(entry);
    entry_free(entry);

    cache_unlock(cache);
    return UCACHE_OK;
}

ucache_result ucache_clear(ucache *cache) {
    if (!cache) return UCACHE_ERR_NULL_CACHE;

    cache_lock(cache);

    ucache_entry *entry = cache->head;
    while (entry) {
        ucache_entry *next = entry->next;
        entry_free(entry);
        entry = next;
    }

    memset(cache->buckets, 0, cache->capacity * sizeof(ucache_entry *));
    cache->head = NULL;
    cache->tail = NULL;
    cache->item_count = 0;
    cache->current_memory = 0;

    cache_unlock(cache);
    return UCACHE_OK;
}

ucache_result ucache_get_stats(ucache *cache, ucache_stats *stats) {
    if (!cache) return UCACHE_ERR_NULL_CACHE;
    if (!stats) return UCACHE_OK;

    cache_lock(cache);

    stats->item_count = cache->item_count;
    stats->max_items = cache->max_items;
    stats->current_memory = cache->current_memory;
    stats->max_memory = cache->max_memory;

    if (cache->enable_stats) {
        stats->hit_count = cache->hit_count;
        stats->miss_count = cache->miss_count;
        stats->eviction_count = cache->eviction_count;
    } else {
        stats->hit_count = 0;
        stats->miss_count = 0;
        stats->eviction_count = 0;
    }

    cache_unlock(cache);
    return UCACHE_OK;
}

const char *ucache_result_string(ucache_result result) {
    switch (result) {
        case UCACHE_OK:             return "Success";
        case UCACHE_ERR_NULL_CACHE: return "Cache is NULL";
        case UCACHE_ERR_NULL_KEY:   return "Key is NULL or empty";
        case UCACHE_ERR_NULL_VALUE: return "Value is NULL";
        case UCACHE_ERR_NOT_FOUND:  return "Key not found";
        case UCACHE_ERR_MEMORY:     return "Memory allocation failed";
        default:                      return "Unknown error";
    }
}
