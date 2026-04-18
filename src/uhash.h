//
//  uhash.h
//
//  Created by kejinlu on 2026-04-19.
//
//  Hash functions: MurmurHash3 (32-bit).
//

#ifndef uhash_h
#define uhash_h

#include <stdint.h>
#include <stddef.h>

uint32_t murmur3_32(const void *key, size_t len, uint32_t seed);
#endif /* uhash_h */
