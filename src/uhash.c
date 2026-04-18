//
//  uhash.c
//
//  Created by kejinlu on 2026-04-19.
//
//  Hash functions: MurmurHash3 (32-bit).
//

#include "uhash.h"
#include <string.h>

// ============================================================
// MurmurHash3_x86_32
// Source: https://github.com/aappleby/smhasher
// ============================================================

static inline uint32_t rotl32(uint32_t x, int8_t r) {
    return (x << r) | (x >> (32 - r));
}

static inline uint32_t fmix32(uint32_t h) {
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;
    return h;
}

uint32_t murmur3_32(const void *key, size_t len, uint32_t seed) {
    if (!key) return 0;

    const uint8_t *data = (const uint8_t *)key;
    const size_t nblocks = len / 4;

    uint32_t h1 = seed;

    const uint32_t c1 = 0xcc9e2d51;
    const uint32_t c2 = 0x1b873593;

    // Body - process 4-byte blocks
    for (size_t i = 0; i < nblocks; i++) {
        // Byte-by-byte read to avoid alignment issues
        uint32_t k1 = (uint32_t)data[i * 4 + 0]
                    | ((uint32_t)data[i * 4 + 1] << 8)
                    | ((uint32_t)data[i * 4 + 2] << 16)
                    | ((uint32_t)data[i * 4 + 3] << 24);

        k1 *= c1;
        k1 = rotl32(k1, 15);
        k1 *= c2;

        h1 ^= k1;
        h1 = rotl32(h1, 13);
        h1 = h1 * 5 + 0xe6546b64;
    }

    // Tail - process remaining bytes
    const uint8_t *tail = data + nblocks * 4;
    uint32_t k1 = 0;

    switch (len & 3) {
        case 3: k1 ^= (uint32_t)tail[2] << 16; /* fall through */
        case 2: k1 ^= (uint32_t)tail[1] << 8;  /* fall through */
        case 1: k1 ^= (uint32_t)tail[0];
                k1 *= c1;
                k1 = rotl32(k1, 15);
                k1 *= c2;
                h1 ^= k1;
    }

    // Finalization
    h1 ^= (uint32_t)len;
    h1 = fmix32(h1);

    return h1;
}
