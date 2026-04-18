#include "unity.h"
#include "uobject.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================
// Test helpers
// ============================================================

typedef struct TestObject {
    uobject obj;
    int value;
    bool destroyed;
} TestObject;

static int g_destroy_count = 0;
static int g_retain_hook_count = 0;
static int g_release_hook_count = 0;

static void test_object_release(uobject *obj) {
    TestObject *t = uobject_cast(obj, TestObject, obj);
    t->destroyed = true;
    g_destroy_count++;
    free(t);
}

static void test_object_on_retain(uobject *obj) {
    (void)obj;
    g_retain_hook_count++;
}

static void test_object_on_release(uobject *obj) {
    (void)obj;
    g_release_hook_count++;
}

static uint32_t test_object_hash(uobject *obj) {
    TestObject *t = uobject_cast(obj, TestObject, obj);
    return (uint32_t)t->value;
}

static bool test_object_equal(uobject *a, uobject *b) {
    TestObject *ta = uobject_cast(a, TestObject, obj);
    TestObject *tb = uobject_cast(b, TestObject, obj);
    return ta->value == tb->value;
}

static int test_object_compare(uobject *a, uobject *b) {
    TestObject *ta = uobject_cast(a, TestObject, obj);
    TestObject *tb = uobject_cast(b, TestObject, obj);
    return (ta->value > tb->value) - (ta->value < tb->value);
}

static uint64_t test_object_memory_size(uobject *obj) {
    (void)obj;
    return 999;
}

static const uobject_type test_object_type = {
    .name = "TestObject",
    .size = sizeof(TestObject),
    .release = test_object_release,
};

static const uobject_type test_object_type_with_hooks = {
    .name = "TestObject",
    .size = sizeof(TestObject),
    .release = test_object_release,
    .on_retain = test_object_on_retain,
    .on_release = test_object_on_release,
};

static const uobject_type test_object_type_full = {
    .name = "TestObject",
    .size = sizeof(TestObject),
    .release = test_object_release,
    .hash = test_object_hash,
    .equal = test_object_equal,
    .compare = test_object_compare,
    .memory_size = test_object_memory_size,
};

static const uobject_type type_a = {
    .name = "TypeA",
    .size = sizeof(TestObject),
    .release = test_object_release,
};

static const uobject_type type_b = {
    .name = "TypeB",
    .size = sizeof(TestObject),
    .release = test_object_release,
};

static TestObject *test_object_new(int value, const uobject_type *type) {
    TestObject *t = malloc(sizeof(TestObject));
    TEST_ASSERT_NOT_NULL(t);
    t->value = value;
    t->destroyed = false;
    char name[32];
    snprintf(name, sizeof(name), "test_%d", value);
    uobject_init(&t->obj, type, strdup(name));
    return t;
}

static TestObject *test_object_new_simple(int value) {
    return test_object_new(value, &test_object_type);
}

// ============================================================
// Tests
// ============================================================

static void test_uobject_init_basic(void) {
    TestObject t = {.value = 42, .destroyed = false};
    uobject_init(&t.obj, &test_object_type, "hello");

    TEST_ASSERT_EQUAL_UINT32(1, uobject_refcount(&t.obj));
    TEST_ASSERT_EQUAL_PTR(&test_object_type, t.obj.type);
    TEST_ASSERT_EQUAL_STRING("hello", t.obj.name);
    TEST_ASSERT_FALSE(t.destroyed);
}

static void test_uobject_init_null(void) {
    // Should not crash
    uobject_init(NULL, &test_object_type, "test");
    TestObject t = {.value = 0};
    uobject_init(&t.obj, NULL, "test");
    // type is NULL, refcount should not be set to 1
    TEST_ASSERT_EQUAL_UINT32(0, uobject_refcount(&t.obj));
}

static void test_uobject_retain_release(void) {
    TestObject *t = test_object_new_simple(1);

    TEST_ASSERT_EQUAL_UINT32(1, uobject_refcount(&t->obj));

    uobject_retain(&t->obj);
    TEST_ASSERT_EQUAL_UINT32(2, uobject_refcount(&t->obj));

    uobject_retain(&t->obj);
    TEST_ASSERT_EQUAL_UINT32(3, uobject_refcount(&t->obj));

    bool destroyed = uobject_release(&t->obj);
    TEST_ASSERT_FALSE(destroyed);
    TEST_ASSERT_EQUAL_UINT32(2, uobject_refcount(&t->obj));

    destroyed = uobject_release(&t->obj);
    TEST_ASSERT_FALSE(destroyed);
    TEST_ASSERT_EQUAL_UINT32(1, uobject_refcount(&t->obj));

    // Last release destroys the object
    destroyed = uobject_release(&t->obj);
    TEST_ASSERT_TRUE(destroyed);
    // t is freed, do not access
}

static void test_uobject_retain_null(void) {
    uobject *result = uobject_retain(NULL);
    TEST_ASSERT_NULL(result);
}

static void test_uobject_release_null(void) {
    bool result = uobject_release(NULL);
    TEST_ASSERT_FALSE(result);
}

static void test_uobject_release_destroys(void) {
    g_destroy_count = 0;
    TestObject *t = test_object_new_simple(1);
    TEST_ASSERT_EQUAL_INT(0, g_destroy_count);

    uobject_release(&t->obj);
    TEST_ASSERT_EQUAL_INT(1, g_destroy_count);
}

static void test_uobject_refcount(void) {
    TEST_ASSERT_EQUAL_UINT32(0, uobject_refcount(NULL));

    TestObject *t = test_object_new_simple(1);
    TEST_ASSERT_EQUAL_UINT32(1, uobject_refcount(&t->obj));
    uobject_release(&t->obj);
}

static void test_uobject_equal_same_pointer(void) {
    TestObject *t = test_object_new_simple(1);
    TEST_ASSERT_TRUE(uobject_equal(&t->obj, &t->obj));
    uobject_release(&t->obj);
}

static void test_uobject_equal_null(void) {
    TestObject *t = test_object_new_simple(1);
    TEST_ASSERT_FALSE(uobject_equal(NULL, &t->obj));
    TEST_ASSERT_FALSE(uobject_equal(&t->obj, NULL));
    TEST_ASSERT_TRUE(uobject_equal(NULL, NULL));
    uobject_release(&t->obj);
}

static void test_uobject_equal_no_vtable(void) {
    TestObject *a = test_object_new_simple(1);
    TestObject *b = test_object_new_simple(1);
    // No equal callback, different pointers -> false
    TEST_ASSERT_FALSE(uobject_equal(&a->obj, &b->obj));
    uobject_release(&a->obj);
    uobject_release(&b->obj);
}

static void test_uobject_equal_with_vtable(void) {
    TestObject *a = test_object_new(1, &test_object_type_full);
    TestObject *b = test_object_new(1, &test_object_type_full);
    TestObject *c = test_object_new(2, &test_object_type_full);

    TEST_ASSERT_TRUE(uobject_equal(&a->obj, &b->obj));   // same value
    TEST_ASSERT_FALSE(uobject_equal(&a->obj, &c->obj));  // different value

    uobject_release(&a->obj);
    uobject_release(&b->obj);
    uobject_release(&c->obj);
}

static void test_uobject_compare_null(void) {
    TestObject *t = test_object_new_simple(1);

    TEST_ASSERT_EQUAL_INT(0, uobject_compare(NULL, NULL));
    TEST_ASSERT_TRUE(uobject_compare(NULL, &t->obj) < 0);
    TEST_ASSERT_TRUE(uobject_compare(&t->obj, NULL) > 0);

    uobject_release(&t->obj);
}

static void test_uobject_compare_different_types(void) {
    TestObject *a = test_object_new(1, &type_a);
    TestObject *b = test_object_new(2, &type_b);

    int result = uobject_compare(&a->obj, &b->obj);
    TEST_ASSERT_TRUE(result < 0);  // "TypeA" < "TypeB"

    uobject_release(&a->obj);
    uobject_release(&b->obj);
}

static void test_uobject_compare_same_type_with_vtable(void) {
    TestObject *a = test_object_new(1, &test_object_type_full);
    TestObject *b = test_object_new(3, &test_object_type_full);
    TestObject *c = test_object_new(1, &test_object_type_full);

    TEST_ASSERT_TRUE(uobject_compare(&a->obj, &b->obj) < 0);  // 1 < 3
    TEST_ASSERT_TRUE(uobject_compare(&b->obj, &a->obj) > 0);  // 3 > 1
    TEST_ASSERT_EQUAL_INT(0, uobject_compare(&a->obj, &c->obj)); // 1 == 1

    uobject_release(&a->obj);
    uobject_release(&b->obj);
    uobject_release(&c->obj);
}

static void test_uobject_compare_by_name(void) {
    TestObject *a = test_object_new_simple(1);
    TestObject *b = test_object_new_simple(2);

    int result = uobject_compare(&a->obj, &b->obj);
    // Names are "test_1" and "test_2"
    TEST_ASSERT_TRUE(result < 0);

    uobject_release(&a->obj);
    uobject_release(&b->obj);
}

static void test_uobject_hash_null(void) {
    TEST_ASSERT_EQUAL_UINT32(0, uobject_hash(NULL));
}

static void test_uobject_hash_default(void) {
    TestObject *t = test_object_new_simple(1);
    uint32_t h = uobject_hash(&t->obj);
    // Default: pointer hash, should be non-zero
    TEST_ASSERT_TRUE(h != 0 || (uintptr_t)t == 0);
    uobject_release(&t->obj);
}

static void test_uobject_hash_with_vtable(void) {
    TestObject *t = test_object_new(42, &test_object_type_full);
    TEST_ASSERT_EQUAL_UINT32(42, uobject_hash(&t->obj));
    uobject_release(&t->obj);
}

static void test_uobject_memory_size_default(void) {
    TestObject *t = test_object_new_simple(1);
    TEST_ASSERT_EQUAL_UINT64(sizeof(TestObject), uobject_memory_size(&t->obj));
    uobject_release(&t->obj);
}

static void test_uobject_memory_size_with_vtable(void) {
    TestObject *t = test_object_new(1, &test_object_type_full);
    TEST_ASSERT_EQUAL_UINT64(999, uobject_memory_size(&t->obj));
    uobject_release(&t->obj);
}

static void test_uobject_memory_size_null(void) {
    TEST_ASSERT_EQUAL_UINT64(0, uobject_memory_size(NULL));
}

static void test_uobject_dump_null(void) {
    // Should not crash
    uobject_dump(NULL);
}

static void test_uobject_dump_basic(void) {
    TestObject *t = test_object_new_simple(1);
    // Should not crash
    uobject_dump(&t->obj);
    uobject_release(&t->obj);
}

static void test_uobject_cast(void) {
    TestObject *t = test_object_new_simple(1);
    TestObject *recovered = uobject_cast(&t->obj, TestObject, obj);
    TEST_ASSERT_EQUAL_PTR(t, recovered);
    TEST_ASSERT_EQUAL_INT(1, recovered->value);
    uobject_release(&t->obj);
}

static void test_uobject_on_retain_release_hooks(void) {
    g_retain_hook_count = 0;
    g_release_hook_count = 0;

    TestObject *t = test_object_new(1, &test_object_type_with_hooks);

    TEST_ASSERT_EQUAL_INT(0, g_retain_hook_count);
    TEST_ASSERT_EQUAL_INT(0, g_release_hook_count);

    uobject_retain(&t->obj);
    TEST_ASSERT_EQUAL_INT(1, g_retain_hook_count);

    uobject_retain(&t->obj);
    TEST_ASSERT_EQUAL_INT(2, g_retain_hook_count);

    uobject_release(&t->obj);
    TEST_ASSERT_EQUAL_INT(1, g_release_hook_count);

    uobject_release(&t->obj);
    TEST_ASSERT_EQUAL_INT(2, g_release_hook_count);

    // Last release also triggers on_release hook
    uobject_release(&t->obj);
    TEST_ASSERT_EQUAL_INT(3, g_release_hook_count);
}

// ============================================================
// Test runner
// ============================================================

void run_uobject_tests(void) {
    printf("\n");
    printf("========================================\n");
    printf("  uobject Tests\n");
    printf("========================================\n");

    RUN_TEST(test_uobject_init_basic);
    RUN_TEST(test_uobject_init_null);
    RUN_TEST(test_uobject_retain_release);
    RUN_TEST(test_uobject_retain_null);
    RUN_TEST(test_uobject_release_null);
    RUN_TEST(test_uobject_release_destroys);
    RUN_TEST(test_uobject_refcount);
    RUN_TEST(test_uobject_equal_same_pointer);
    RUN_TEST(test_uobject_equal_null);
    RUN_TEST(test_uobject_equal_no_vtable);
    RUN_TEST(test_uobject_equal_with_vtable);
    RUN_TEST(test_uobject_compare_null);
    RUN_TEST(test_uobject_compare_different_types);
    RUN_TEST(test_uobject_compare_same_type_with_vtable);
    RUN_TEST(test_uobject_compare_by_name);
    RUN_TEST(test_uobject_hash_null);
    RUN_TEST(test_uobject_hash_default);
    RUN_TEST(test_uobject_hash_with_vtable);
    RUN_TEST(test_uobject_memory_size_default);
    RUN_TEST(test_uobject_memory_size_with_vtable);
    RUN_TEST(test_uobject_memory_size_null);
    RUN_TEST(test_uobject_dump_null);
    RUN_TEST(test_uobject_dump_basic);
    RUN_TEST(test_uobject_cast);
    RUN_TEST(test_uobject_on_retain_release_hooks);
}
