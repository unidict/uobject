//
//  uobject.c
//
//  Created by kejinlu on 2026-04-19.
//
//  Linux-kernel-style object system implementation.
//

#include "uobject.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================
// uobject implementation
// ============================================================

void uobject_init(uobject *obj, const uobject_type *type, const char *name) {
    if (!obj || !type)
        return;

    atomic_init(&obj->refcount, 1);
    obj->type = type;
    obj->name = name ? strdup(name) : NULL;
}

uobject *uobject_retain(uobject *obj) {
    if (!obj)
        return NULL;

    // No overflow protection: reaching UINT32_MAX references is unrealistic.
    // Same approach as GLib (g_object_ref), Chromium (scoped_refptr),
    // and Linux kernel's legacy kref_get.
    atomic_fetch_add_explicit(&obj->refcount, 1, memory_order_relaxed);

    // Invoke on_retain hook
    if (obj->type && obj->type->on_retain) {
        obj->type->on_retain(obj);
    }

    return obj;
}

bool uobject_release(uobject *obj) {
    if (!obj)
        return false;

    // Invoke on_release hook
    if (obj->type && obj->type->on_release) {
        obj->type->on_release(obj);
    }

    // Decrement refcount; if it reaches zero, call the release function.
    if (atomic_fetch_sub_explicit(&obj->refcount, 1,
                                  memory_order_acq_rel) == 1) {
        free(obj->name);
        if (obj->type && obj->type->release) {
            obj->type->release(obj);
        }
        return true;
    }
    return false;
}

uint32_t uobject_refcount(const uobject *obj) {
    if (!obj)
        return 0;
    return atomic_load(&obj->refcount);
}

// ============================================================
// Virtual function dispatch implementation
// ============================================================

void uobject_dump(uobject *obj) {
    if (!obj) {
        printf("uobject: NULL\n");
        return;
    }

    printf("uobject[%s] \"%s\" (refcount=%u)\n",
           obj->type ? obj->type->name : "(unknown)",
           obj->name ? obj->name : "(unnamed)",
           uobject_refcount(obj));

    // Dispatch to type-specific dump
    if (obj->type && obj->type->dump) {
        obj->type->dump(obj);
    }
}

int uobject_compare(uobject *a, uobject *b) {
    if (!a && !b)
        return 0;
    if (!a)
        return -1;
    if (!b)
        return 1;

    // If types differ, compare by type name
    if (a->type != b->type) {
        if (!a->type && !b->type)
            return 0;
        if (!a->type)
            return -1;
        if (!b->type)
            return 1;
        return strcmp(a->type->name, b->type->name);
    }

    // Dispatch to type-specific compare
    if (a->type && a->type->compare) {
        return a->type->compare(a, b);
    }

    // Default: compare by name
    const char *name_a = a->name ? a->name : "";
    const char *name_b = b->name ? b->name : "";
    return strcmp(name_a, name_b);
}

bool uobject_equal(uobject *a, uobject *b) {
    if (a == b)
        return true;
    if (!a || !b)
        return false;

    // Dispatch to type-specific equal
    if (a->type && a->type->equal) {
        return a->type->equal(a, b);
    }

    // Default: pointer comparison
    return false;
}

uint32_t uobject_hash(uobject *obj) {
    if (!obj)
        return 0;

    // Dispatch to type-specific hash
    if (obj->type && obj->type->hash) {
        return obj->type->hash(obj);
    }

    // Default: pointer hash (shift right by 2 to skip alignment zeros)
    return (uint32_t)((uintptr_t)obj >> 2);
}

uint64_t uobject_memory_size(uobject *obj) {
    if (!obj)
        return 0;

    // Dispatch to type-specific memory_size
    if (obj->type && obj->type->memory_size) {
        return obj->type->memory_size(obj);
    }

    // Default: use type->size
    if (obj->type) {
        return (uint64_t)obj->type->size;
    }

    return 0;
}
