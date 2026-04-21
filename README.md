# uobject

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C](https://img.shields.io/badge/C-11-blue.svg)](https://en.wikipedia.org/wiki/C11_(C_standard))
[![CI](https://github.com/unidict/uobject/actions/workflows/ci.yml/badge.svg)](https://github.com/unidict/uobject/actions/workflows/ci.yml)

**libuobject** — A minimal C library providing Linux-kernel-style reference-counted objects with type-safe polymorphism, virtual function tables, and an LRU cache.

## Features

- **Unified reference counting** — Atomic refcount embedded in every object; automatic destruction via `type->release` when count reaches zero
- **Type-safe polymorphism without inheritance** — Composition via `uobject` embedding; `uobject_cast` (container_of) with compile-time type checking on GCC/Clang
- **Runtime type information via vtable** — Static `uobject_type` with virtual functions: release, dump, compare, equal, hash, memory_size
- **LRU cache with memory limits** — `ucache` provides key-value caching with LRU eviction, memory/item limits, optional thread safety, and hit/miss statistics
- **Cross-platform** — C11; works on Linux, macOS, and Windows

## Building

### Prerequisites

- C compiler with C11 support (GCC, Clang, MSVC)
- CMake 3.14+ (optional, for build system)

### Build from Source

```bash
git clone https://github.com/kejinlu/uobject.git
cd uobject

# Configure
cmake -B build

# Build
cmake --build build

# Run tests
ctest --test-dir build --output-on-failure
```

#### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `UOBJECT_BUILD_TESTS` | `ON` | Build test suite |
| `BUILD_SHARED_LIBS` | `OFF` | Build shared library instead of static |

**Example: Disable tests and build shared library**
```bash
cmake -DUOBJECT_BUILD_TESTS=OFF -DBUILD_SHARED_LIBS=ON -B build
```

### Compile Directly (Without CMake)

```bash
# Compile the library
gcc -c src/uobject.c src/ucache.c src/uhash.c -I./src -std=c11 -Wall -Wextra

# Link with your program
gcc -o myprogram myprogram.c uobject.o ucache.o uhash.o -lpthread
```

## Quick Start

### Defining a Custom Object

```c
#include "uobject.h"
#include <stdlib.h>
#include <stdio.h>

typedef struct User {
    uobject obj;       // Must be the first member
    int user_id;
    char username[64];
} User;

static void user_release(uobject *obj) {
    User *user = uobject_cast(obj, User, obj);
    free(user);
}

static const uobject_type user_type = {
    .name = "User",
    .size = sizeof(User),
    .release = user_release,
};

User *user_new(int id, const char *name) {
    User *user = malloc(sizeof(User));
    user->user_id = id;
    snprintf(user->username, sizeof(user->username), "%s", name);
    uobject_init(&user->obj, &user_type, name);
    return user;
}
```

### Reference Counting

```c
User *user = user_new(1, "alice");
// refcount = 1

uobject_retain(&user->obj);
// refcount = 2

uobject_release(&user->obj);
// refcount = 1, object still alive

uobject_release(&user->obj);
// refcount = 0, user_release() called, memory freed
```

### Using the LRU Cache

```c
#include "ucache.h"

ucache_config config = {
    .max_items = 1000,
    .max_memory = 0,         // no memory limit
    .thread_safe = true,
    .enable_stats = true,
};

ucache *cache = ucache_new(&config);

User *user = user_new(1, "alice");
ucache_set(cache, "alice", 5, &user->obj);

uobject *result = NULL;
if (ucache_get_retain(cache, "alice", 5, &result) == UCACHE_OK) {
    User *found = uobject_cast(result, User, obj);
    printf("Found user: %s (id=%d)\n", found->username, found->user_id);
    uobject_release(result);  // release the get_retain reference
}

uobject_release(&user->obj);  // release our reference
ucache_free(cache);
```

## API Reference

### uobject (Core)

| Function | Description |
|----------|-------------|
| `uobject_init(obj, type, name)` | Initialize object with refcount=1 |
| `uobject_retain(obj)` | Increment refcount, return obj |
| `uobject_release(obj)` | Decrement refcount; destroy at 0 |
| `uobject_refcount(obj)` | Get current refcount |
| `uobject_dump(obj)` | Debug print object info |
| `uobject_compare(a, b)` | Compare two objects |
| `uobject_equal(a, b)` | Check equality |
| `uobject_hash(obj)` | Compute hash value |
| `uobject_memory_size(obj)` | Get memory footprint |
| `uobject_cast(ptr, type, member)` | container_of with type checking |
| `uobject_cast_const(ptr, type, member)` | Const-preserving container_of |
| `uobject_retain_typed(ptr, type)` | Type-safe retain |
| `uobject_release_typed(ptr, type)` | Type-safe release |

### ucache (LRU Cache)

| Function | Description |
|----------|-------------|
| `ucache_new(config)` | Create a new cache |
| `ucache_free(cache)` | Free cache and release all values |
| `ucache_set(cache, key, len, value)` | Insert/update a key-value pair |
| `ucache_get_retain(cache, key, len, &value)` | Get value (refcount incremented) |
| `ucache_exists(cache, key, len, &exists)` | Check if key exists |
| `ucache_delete(cache, key, len)` | Delete a key |
| `ucache_clear(cache)` | Remove all entries |
| `ucache_get_stats(cache, &stats)` | Get hit/miss/eviction statistics |
| `ucache_result_string(result)` | Convert error code to string |

### uhash

| Function | Description |
|----------|-------------|
| `murmur3_32(key, len, seed)` | MurmurHash3 (32-bit) hash function |

## Platform Support

- **Linux** (tested with GCC, Clang)
- **macOS** (tested with Clang)
- **Windows** (MSVC 2022+, MinGW)

## License

MIT License. See [LICENSE](LICENSE) for details.
