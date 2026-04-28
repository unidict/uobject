//
//  uobject.h
//
//  Created by kejinlu on 2026-04-19.
//
//  Similar to kobject in the Linux kernel.
//
//  Goals:
//  1. Unified reference counting
//     - Atomic refcount embedded in every object
//     - Automatic destruction via type->release when count reaches zero
//     - Optional on_retain/on_release hooks for debugging and lifecycle tracking
//
//  2. Type-safe polymorphism without inheritance
//     - Composition: embed uobject as a member in user structs
//     - uobject_cast recovers the enclosing struct from an embedded uobject
//       pointer, with compile-time type checking on GCC/Clang
//       (similar to Linux kernel's container_of pattern)
//     - uobject_retain_typed / uobject_release_typed operate on derived
//       pointers directly, with the same type checking
//     - Falls back to unchecked versions on MSVC
//
//  3. Runtime type information via vtable (uobject_type)
//     - Each type registers a static const uobject_type with virtual
//       functions: release, dump, compare, equal, hash, memory_size
//     - Generic APIs (uobject_hash, uobject_equal, ...) dispatch through
//       the vtable, so callers never need to know the concrete type
//     - Default implementations provided for all optional virtuals
//
//  4. Cross-platform (macOS / Linux / Windows)
//     - C11 with compiler-specific fallbacks for MSVC
//     - Thread-safe via _Atomic operations; no platform-specific locks
//       in the core object system
//

#ifndef uobject_h
#define uobject_h

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// uobject_type - Type info / vtable (similar to kobj_type)
// ============================================================

typedef struct uobject uobject;

/**
 * uobject_type - Type information and virtual function table
 *
 * Provides type information and a virtual function table,
 * enabling polymorphism. Similar to a C++ vtable or an OOP interface.
 *
 * Required:
 *   - release: Called when the object is destroyed
 *
 * Optional:
 *   - dump:         Debug output
 *   - compare:      Object comparison (for sorting)
 *   - equal:        Equality check (for hash tables)
 *   - hash:         Hash computation (for hash tables)
 *   - memory_size:  Memory footprint query (for cache memory limits)
 *   - on_retain/on_release: Reference count hooks (for debugging, logging)
 *
 * Example:
 *   static void my_type_release(uobject *my_obj) {
 *       MyType *t = uobject_cast(my_obj, MyType, obj);
 *       free(t);
 *   }
 *
 *   static const uobject_type my_type = {
 *       .name = "MyType",
 *       .size = sizeof(MyType),
 *       .release = my_type_release,
 *   };
 */
typedef struct uobject_type {
    const char *name;
    size_t      size;           // sizeof the enclosing struct; used as default by uobject_memory_size

    // Required
    void (*release)(uobject *obj);

    // Optional virtual functions
    void     (*dump)(uobject *obj);
    int      (*compare)(uobject *a, uobject *b);
    bool     (*equal)(uobject *a, uobject *b);
    uint32_t (*hash)(uobject *obj);
    uint64_t (*memory_size)(uobject *obj);

    // Optional hooks
    void (*on_retain)(uobject *obj);
    void (*on_release)(uobject *obj);
} uobject_type;

// ============================================================
// uobject - Base object (similar to kobject)
// ============================================================

/**
 * uobject - Base object for all reference-counted objects
 *
 * The foundational struct for all objects using reference counting
 * and virtual functions. Similar to Linux kernel's kobject or a
 * base class in OOP languages.
 *
 * Design pattern:
 *   - Embed uobject in user structs (composition, not inheritance)
 *   - Use uobject_cast to obtain the enclosing struct from a uobject*
 *     (container_of pattern)
 *   - Reference counting and virtual functions are managed uniformly
 *     through uobject
 *
 * Example:
 *   static void user_release(uobject *obj) {
 *       User *user = uobject_cast(obj, User, obj);
 *       free(user);
 *   }
 *
 *   static const uobject_type user_type = {
 *       .name = "User",
 *       .size = sizeof(User),
 *       .release = user_release,
 *   };
 *
 *   typedef struct User {
 *       uobject obj;
 *       int user_id;
 *       char username[64];
 *   } User;
 *
 *   User *user = malloc(sizeof(User));
 *   uobject_init(&user->obj, &user_type, "alice");
 *   uobject_retain(&user->obj);
 *   uobject_release(&user->obj);
 *
 * Note:
 *   - The name string is internally copied (strdup) and freed on release
 *   - Caller can safely pass stack-allocated or temporary strings
 */
struct uobject {
    const uobject_type *type;
    _Atomic uint32_t    refcount;
    char               *name;
};

// ============================================================
// Object lifecycle
// ============================================================

/**
 * uobject_init - Initialize a uobject
 * @obj:  Object pointer to initialize (must not be NULL)
 * @type: Object type (must not be NULL, must point to a static const uobject_type)
 * @name: Object name (may be NULL; must be a static string or managed by the caller)
 *
 * Sets reference count to 1, associates type info and name.
 * After initialization the caller holds the initial reference.
 *
 * Note: @obj and @type must not be NULL. Passing NULL is a programmer error
 * and results in undefined behavior (the function silently returns).
 *
 * Name ownership:
 *   - The name string is copied internally (strdup) and owned by uobject
 *   - Safe to pass stack-allocated or temporary strings
 *   - Set to NULL if name is NULL or allocation fails
 */
void uobject_init(uobject *obj, const uobject_type *type, const char *name);

// ============================================================
// Reference counting
// ============================================================

/**
 * uobject_retain - Increment reference count
 * @obj: Object pointer
 *
 * Increments the reference count by 1.
 *
 * Return: The object pointer (NULL if @obj is NULL)
 */
uobject *uobject_retain(uobject *obj);

/**
 * uobject_release - Decrement reference count
 * @obj: Object pointer
 *
 * Decrements the reference count by 1. If the count reaches zero,
 * calls type->release to destroy the object.
 *
 * Return: true if the object was destroyed (count reached zero)
 *
 * Note: Do not access the object after this call (unless it returned false)
 */
bool uobject_release(uobject *obj);

/**
 * uobject_refcount - Get current reference count
 * @obj: Object pointer
 *
 * Return: Current reference count (0 if @obj is NULL)
 */
uint32_t uobject_refcount(const uobject *obj);

// ============================================================
// Virtual function dispatch
// ============================================================

/**
 * uobject_dump - Print object info for debugging.
 * Prints basic info (type, name, refcount) and calls type->dump if set.
 */
void uobject_dump(uobject *obj);

/**
 * uobject_compare - Compare two objects.
 * Dispatches to type->compare if set; falls back to type name or
 * name comparison. NULL is treated as less than any non-NULL value.
 *
 * Return: < 0 if a < b, 0 if a == b, > 0 if a > b
 */
int uobject_compare(uobject *a, uobject *b);

/**
 * uobject_equal - Check if two objects are equal.
 * Dispatches to type->equal if set; returns false by default.
 */
bool uobject_equal(uobject *a, uobject *b);

/**
 * uobject_hash - Compute hash value of an object.
 * Dispatches to type->hash if set; falls back to pointer hashing.
 *
 * Return: 32-bit hash value
 */
uint32_t uobject_hash(uobject *obj);

/**
 * uobject_memory_size - Get the memory footprint of an object.
 * Dispatches to type->memory_size if set; falls back to type->size.
 *
 * Return: Memory footprint in bytes
 */
uint64_t uobject_memory_size(uobject *obj);

// ============================================================
// Macros
// ============================================================

// Internal: type compatibility check (GCC/Clang only).
//
// __typeof__ accepts both expressions and type names:
//   __typeof__(expr)       -> yields the type of the expression
//   __typeof__(type-name)  -> yields the type itself (identity)
//
// __builtin_types_compatible_p compares UNQUALIFIED types, so:
//   U_SAME_TYPE(const T, T)    -> 1  (const is ignored)
//   U_SAME_TYPE(volatile T, T) -> 1  (volatile is ignored)
#ifndef _MSC_VER
#define U_SAME_TYPE(a, b) __builtin_types_compatible_p(__typeof__(a), __typeof__(b))
#endif

/**
 * uobject_cast - container_of: obtain enclosing struct from uobject
 * @ptr:    Pointer to the embedded uobject member
 * @type:   Type of the enclosing struct
 * @member: Name of the uobject member within @type
 *
 * Uses offsetof to compute the enclosing struct pointer from an
 * embedded uobject pointer. Similar to Linux kernel's container_of.
 *
 * Return: Pointer to the enclosing struct
 *
 * Example:
 *   void process_user(uobject *user_obj) {
 *       User *user = uobject_cast(user_obj, User, obj);
 *       printf("User ID: %d\n", user->id);
 *   }
 *
 * Type checking (GCC/Clang only):
 *   Compile-time check via __builtin_types_compatible_p.
 *   MSVC falls back to unchecked version.
 */
#ifndef _MSC_VER
#define uobject_cast(ptr, type, member)                                        \
    __extension__({                                                            \
        _Static_assert(U_SAME_TYPE(*(ptr), ((type *)0)->member) ||             \
                           U_SAME_TYPE(*(ptr), void),                          \
                       "pointer type mismatch in uobject_cast()");             \
        (type *)((char *)(ptr) - offsetof(type, member));                      \
    })
#else
#define uobject_cast(ptr, type, member)                                        \
    ((type *)((char *)(ptr) - offsetof(type, member)))
#endif

/**
 * uobject_cast_const - Const-preserving container_of
 * @ptr:    Pointer to the embedded uobject member
 * @type:   Type of the enclosing struct
 * @member: Name of the uobject member within @type
 *
 * Like uobject_cast, but preserves const-ness: if @ptr is a
 * pointer to const, the return type is also const-qualified.
 *
 * Return: Pointer to the enclosing struct (const-qualified if @ptr is)
 *
 * Example:
 *   const uobject *cobj = ...;
 *   const User *user = uobject_cast_const(cobj, User, obj);
 *
 *   uobject *obj = ...;
 *   User *user2 = uobject_cast_const(obj, User, obj);
 */
#ifndef _MSC_VER
// Adapted from Linux kernel's container_of_const (include/linux/container_of.h).
//
// The trick is the leading 'const' in the association type:
//
//   const __typeof__(*(ptr)) *
//
// When ptr is 'T *' (non-const):
//   *(ptr) -> T, so the association type becomes 'const T *'.
//   _Generic compares 'T *' (ptr's type) against 'const T *' -> no match.
//   Falls through to default -> returns 'type *'.
//
// When ptr is 'const T *':
//   *(ptr) -> const T, so the association type becomes 'const const T *'.
//   C standard (C11 6.7.3p4) folds duplicate qualifiers -> 'const T *'.
//   _Generic compares 'const T *' against 'const T *' -> match!
//   Returns 'const type *'.
//
// Without the leading 'const', both cases would produce 'T *' in the
// association, making them indistinguishable.
#define uobject_cast_const(ptr, type, member)                                   \
    _Generic((ptr),                                                             \
        const __typeof__(*(ptr)) *:                                             \
            ((const type *)((char *)(ptr) - offsetof(type, member))),           \
        default:                                                                \
            ((type *)((char *)(ptr) - offsetof(type, member)))                  \
    )
#else
#define uobject_cast_const(ptr, type, member)                                   \
    ((type *)((char *)(ptr) - offsetof(type, member)))
#endif

// Type-safe retain/release convenience macros.
//
// Operate on derived struct pointers directly, without manually
// extracting the embedded uobject member. Assumes the uobject
// member is named "obj".
//
// On GCC/Clang, a compile-time type check is performed via
// __builtin_types_compatible_p to ensure @ptr is a pointer to
// @type. On MSVC the check is skipped.
//
// Example:
//   User *user = malloc(sizeof(User));
//   uobject_init(&user->obj, &user_type, "alice");
//
//   uobject_retain_typed(user, User);     // instead of uobject_retain(&user->obj)
//   uobject_release_typed(user, User);    // instead of uobject_release(&user->obj)
#ifndef _MSC_VER
// Compile-time type check helper (GCC/Clang, like Linux kernel)
#define uobject_type_check(ptr, type)                                          \
    __extension__({                                                            \
        _Static_assert(U_SAME_TYPE(*(ptr), type) ||                            \
                           U_SAME_TYPE(*(ptr), void),                          \
                       "pointer type mismatch");                               \
        (ptr);                                                                 \
    })

#define uobject_retain_typed(ptr, type)                                        \
    __extension__({                                                            \
        __typeof__(ptr) _p = uobject_type_check(ptr, type);                    \
        uobject_retain(&(_p)->obj);                                            \
    })

#define uobject_release_typed(ptr, type)                                       \
    do {                                                                       \
        __typeof__(ptr) _p = uobject_type_check(ptr, type);                    \
        uobject_release(&(_p)->obj);                                           \
    } while (0)
#else
#define uobject_type_check(ptr, type) (ptr)
#define uobject_retain_typed(ptr, type) uobject_retain(&(ptr)->obj)
#define uobject_release_typed(ptr, type) uobject_release(&(ptr)->obj)
#endif

#ifdef __cplusplus
}
#endif

#endif /* uobject_h */
