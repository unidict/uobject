#include "unity.h"
#include "uhash.h"
#include <string.h>
#include <stdio.h>

// ============================================================
// Tests
// ============================================================

static void test_murmur3_null_key(void) {
    TEST_ASSERT_EQUAL_UINT32(0, murmur3_32(NULL, 10, 0));
}

static void test_murmur3_empty_key(void) {
    uint32_t h1 = murmur3_32("", 0, 0);
    uint32_t h2 = murmur3_32("", 0, 0);
    TEST_ASSERT_EQUAL_UINT32(h1, h2);
    // Empty string with seed 0 produces 0 (seed=0, no blocks, fmix(0)=0)
    TEST_ASSERT_EQUAL_UINT32(0, h1);
}

static void test_murmur3_deterministic(void) {
    const char *data = "hello world";
    uint32_t h1 = murmur3_32(data, strlen(data), 42);
    uint32_t h2 = murmur3_32(data, strlen(data), 42);
    TEST_ASSERT_EQUAL_UINT32(h1, h2);
}

static void test_murmur3_different_seeds(void) {
    const char *data = "hello world";
    uint32_t h1 = murmur3_32(data, strlen(data), 0);
    uint32_t h2 = murmur3_32(data, strlen(data), 1);
    TEST_ASSERT_TRUE(h1 != h2);
}

static void test_murmur3_different_lengths(void) {
    uint32_t h1 = murmur3_32("abc", 3, 0);
    uint32_t h2 = murmur3_32("abcd", 4, 0);
    TEST_ASSERT_TRUE(h1 != h2);
}

static void test_murmur3_known_values(void) {
    // Empty string with seed 0 → 0 (trivial case: seed=0, no data)
    TEST_ASSERT_EQUAL_UINT32(0, murmur3_32("", 0, 0));

    // "abc" with seed 0
    uint32_t h = murmur3_32("abc", 3, 0);
    TEST_ASSERT_TRUE(h != 0);  // Non-trivial output
}

static void test_murmur3_different_inputs(void) {
    uint32_t h1 = murmur3_32("foo", 3, 0);
    uint32_t h2 = murmur3_32("bar", 3, 0);
    uint32_t h3 = murmur3_32("baz", 3, 0);
    // All should be different
    TEST_ASSERT_TRUE(h1 != h2);
    TEST_ASSERT_TRUE(h2 != h3);
    TEST_ASSERT_TRUE(h1 != h3);
}

static void test_murmur3_long_key(void) {
    // Key longer than 4 bytes (tests body + tail)
    const char *key = "The quick brown fox jumps over the lazy dog";
    uint32_t h = murmur3_32(key, strlen(key), 12345);
    TEST_ASSERT_TRUE(h != 0);

    // Same key should give same result
    uint32_t h2 = murmur3_32(key, strlen(key), 12345);
    TEST_ASSERT_EQUAL_UINT32(h, h2);
}

static void test_murmur3_binary_key(void) {
    // Binary data with null bytes
    unsigned char data[] = {0x00, 0x01, 0x02, 0x03, 0xFF, 0xFE};
    uint32_t h = murmur3_32(data, sizeof(data), 0);
    TEST_ASSERT_TRUE(h != 0);
}

// ============================================================
// Test runner
// ============================================================

void run_uhash_tests(void) {
    printf("\n");
    printf("========================================\n");
    printf("  uhash Tests\n");
    printf("========================================\n");

    RUN_TEST(test_murmur3_null_key);
    RUN_TEST(test_murmur3_empty_key);
    RUN_TEST(test_murmur3_deterministic);
    RUN_TEST(test_murmur3_different_seeds);
    RUN_TEST(test_murmur3_different_lengths);
    RUN_TEST(test_murmur3_known_values);
    RUN_TEST(test_murmur3_different_inputs);
    RUN_TEST(test_murmur3_long_key);
    RUN_TEST(test_murmur3_binary_key);
}
