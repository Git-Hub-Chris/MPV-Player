/* Copyright (C) 2017 the mpv developers
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#include "osdep/atomic.h"
#include "osdep/strnlen.h"

#define TA_NO_WRAPPERS
#include "ta.h"

// Return element_size * count. If it overflows, return (size_t)-1 (SIZE_MAX).
// I.e. this returns the equivalent of: MIN(element_size * count, SIZE_MAX).
// The idea is that every real memory allocator will reject (size_t)-1, thus
// this is a valid way to handle too large array allocation requests.
size_t ta_calc_array_size(size_t element_size, size_t count)
{
    if (count > (((size_t)-1) / element_size))
        return (size_t)-1;
    return element_size * count;
}

// This is used when an array has to be enlarged for appending new elements.
// Return a "good" size for the new array (in number of elements). This returns
// a value > nextidx, unless the calculation overflows, in which case SIZE_MAX
// is returned.
size_t ta_calc_prealloc_elems(size_t nextidx)
{
    if (nextidx >= ((size_t)-1) / 2 - 1)
        return (size_t)-1;
    return (nextidx + 1) * 2;
}

/* Create an empty (size 0) TA allocation.
 */
void *ta_new_context(void *ta_parent)
{
    return ta_alloc_size(ta_parent, 0);
}

/* Set parent of ptr to ta_parent, return the ptr.
 * Note that ta_parent==NULL will simply unset the current parent of ptr.
 */
void *ta_steal_(void *ta_parent, void *ptr)
{
    ta_set_parent(ptr, ta_parent);
    return ptr;
}

/* Duplicate the memory at ptr with the given size.
 */
void *ta_memdup(void *ta_parent, void *ptr, size_t size)
{
    if (!ptr) {
        assert(!size);
        return NULL;
    }
    void *res = ta_alloc_size(ta_parent, size);
    if (!res)
        return NULL;
    memcpy(res, ptr, size);
    return res;
}

// *str = *str[0..at] + append[0..append_len]
// (append_len being a maximum length; shorter if embedded \0s are encountered)
static bool strndup_append_at(char **str, size_t at, const char *append,
                              size_t append_len)
{
    assert(ta_get_size(*str) >= at);

    if (!*str && !append)
        return true; // stays NULL, but not an OOM condition

    size_t real_len = append ? strnlen(append, append_len) : 0;
    if (append_len > real_len)
        append_len = real_len;

    if (ta_get_size(*str) < at + append_len + 1) {
        char *t = ta_realloc_size(NULL, *str, at + append_len + 1);
        if (!t)
            return false;
        *str = t;
    }

    if (append_len)
        memcpy(*str + at, append, append_len);

    (*str)[at + append_len] = '\0';

    ta_dbg_mark_as_string(*str);

    return true;
}

/* Return a copy of str.
 * Returns NULL on OOM.
 */
char *ta_strdup(void *ta_parent, const char *str)
{
    return ta_strndup(ta_parent, str, str ? strlen(str) : 0);
}

/* Return a copy of str. If the string is longer than n, copy only n characters
 * (the returned allocation will be n+1 bytes and contain a terminating '\0').
 * The returned string will have the length MIN(strlen(str), n)
 * If str==NULL, return NULL. Returns NULL on OOM as well.
 */
char *ta_strndup(void *ta_parent, const char *str, size_t n)
{
    if (!str)
        return NULL;
    char *new = NULL;
    strndup_append_at(&new, 0, str, n);
    ta_set_parent(new, ta_parent);
    return new;
}

/* Append a to *str. If *str is NULL, the string is newly allocated, otherwise
 * ta_realloc() is used on *str as needed.
 * Return success or failure (it can fail due to OOM only).
 */
bool ta_strdup_append(char **str, const char *a)
{
    return strndup_append_at(str, *str ? strlen(*str) : 0, a, (size_t)-1);
}

/* Like ta_strdup_append(), but use ta_get_size(*str)-1 instead of strlen(*str).
 * (See also: ta_asprintf_append_buffer())
 */
bool ta_strdup_append_buffer(char **str, const char *a)
{
    size_t size = ta_get_size(*str);
    if (size > 0)
        size -= 1;
    return strndup_append_at(str, size, a, (size_t)-1);
}

/* Like ta_strdup_append(), but limit the length of a with n.
 * (See also: ta_strndup())
 */
bool ta_strndup_append(char **str, const char *a, size_t n)
{
    return strndup_append_at(str, *str ? strlen(*str) : 0, a, n);
}

/* Like ta_strdup_append_buffer(), but limit the length of a with n.
 * (See also: ta_strndup())
 */
bool ta_strndup_append_buffer(char **str, const char *a, size_t n)
{
    size_t size = ta_get_size(*str);
    if (size > 0)
        size -= 1;
    return strndup_append_at(str, size, a, n);
}

TA_PRF(3, 0)
static bool ta_vasprintf_append_at(char **str, size_t at, const char *fmt,
                                   va_list ap)
{
    assert(ta_get_size(*str) >= at);

    int size;
    va_list copy;
    va_copy(copy, ap);
    char c;
    size = vsnprintf(&c, 1, fmt, copy);
    va_end(copy);

    if (size < 0)
        return false;

    if (ta_get_size(*str) < at + size + 1) {
        char *t = ta_realloc_size(NULL, *str, at + size + 1);
        if (!t)
            return false;
        *str = t;
    }
    vsnprintf(*str + at, size + 1, fmt, ap);

    ta_dbg_mark_as_string(*str);

    return true;
}

/* Like snprintf(); returns the formatted string as allocation (or NULL on OOM
 * or snprintf() errors).
 */
char *ta_asprintf(void *ta_parent, const char *fmt, ...)
{
    char *res;
    va_list ap;
    va_start(ap, fmt);
    res = ta_vasprintf(ta_parent, fmt, ap);
    va_end(ap);
    return res;
}

char *ta_vasprintf(void *ta_parent, const char *fmt, va_list ap)
{
    char *res = NULL;
    ta_vasprintf_append_at(&res, 0, fmt, ap);
    ta_set_parent(res, ta_parent);
    if (!res) {
        ta_free(res);
        return NULL;
    }
    return res;
}

/* Append the formatted string to *str (after strlen(*str)). The allocation is
 * ta_realloced if needed.
 * Returns false on OOM or snprintf() errors, with *str left untouched.
 */
bool ta_asprintf_append(char **str, const char *fmt, ...)
{
    bool res;
    va_list ap;
    va_start(ap, fmt);
    res = ta_vasprintf_append(str, fmt, ap);
    va_end(ap);
    return res;
}

bool ta_vasprintf_append(char **str, const char *fmt, va_list ap)
{
    return ta_vasprintf_append_at(str, *str ? strlen(*str) : 0, fmt, ap);
}

/* Append the formatted string at the end of the allocation of *str. It
 * overwrites the last byte of the allocation too (which is assumed to be the
 * '\0' terminating the string). Compared to ta_asprintf_append(), this is
 * useful if you know that the string ends with the allocation, so that the
 * extra strlen() can be avoided for better performance.
 * Returns false on OOM or snprintf() errors, with *str left untouched.
 */
bool ta_asprintf_append_buffer(char **str, const char *fmt, ...)
{
    bool res;
    va_list ap;
    va_start(ap, fmt);
    res = ta_vasprintf_append_buffer(str, fmt, ap);
    va_end(ap);
    return res;
}

bool ta_vasprintf_append_buffer(char **str, const char *fmt, va_list ap)
{
    size_t size = ta_get_size(*str);
    if (size > 0)
        size -= 1;
    return ta_vasprintf_append_at(str, size, fmt, ap);
}

void *ta_xmemdup(void *ta_parent, void *ptr, size_t size)
{
    void *new = ta_memdup(ta_parent, ptr, size);
    ta_oom_b(new || !ptr);
    return new;
}

void *ta_xrealloc_size(void *ta_parent, void *ptr, size_t size)
{
    ptr = ta_realloc_size(ta_parent, ptr, size);
    ta_oom_b(ptr || !size);
    return ptr;
}

char *ta_xstrdup(void *ta_parent, const char *str)
{
    char *res = ta_strdup(ta_parent, str);
    ta_oom_b(res || !str);
    return res;
}

char *ta_xstrndup(void *ta_parent, const char *str, size_t n)
{
    char *res = ta_strndup(ta_parent, str, n);
    ta_oom_b(res || !str);
    return res;
}

struct ta_refcount {
    atomic_ulong count;
    void *child;
    void (*on_free)(void *ctx, void *ta_child);
    void *on_free_ctx;
};

// Allocate a refcount helper. The ta_child parameter is automatically freed
// with ta_free(). The returned object has a refcount of 1. Once the refcount
// reaches 0, ta_free(ta_child) is called.
//
// If on_free() is not NULL, then instead of ta_free(), on_free() is called with
// the parameters of the same named as passed to this function.
//
// The ta_child pointer must not be NULL, and must not have a parent allocation.
// Neither make any sense for this use-case, and could be hint for serious bugs
// in the caller, so assert()s check for this.
//
// The return value is not a ta allocation. You use the refcount functions to
// manage it. It is recommended to use ta_alloc_auto_unref().
//
// Note: normally you use the ta_refcount_alloc() macro, which lacks the loc
//       parameter.
struct ta_refcount *ta_refcount_alloc_(const char *loc, void *ta_child,
                                       void (*on_free)(void *ctx, void *ta_child),
                                       void *free_ctx)
{
    assert(ta_child && !ta_get_parent(ta_child));

    struct ta_refcount *rc = ta_new_ptrtype(NULL, rc);
    if (!rc)
        return NULL;

    ta_dbg_set_loc(rc, loc);
    *rc = (struct ta_refcount){
        .count = ATOMIC_VAR_INIT(1),
        .child = ta_child,
        .on_free = on_free,
        .on_free_ctx = free_ctx,
    };

    return rc + 1; // "obfuscate" the pointer
}

void ta_refcount_add(struct ta_refcount *rc)
{
    assert(rc);
    rc -= 1;

    unsigned long c = atomic_fetch_add(&rc->count, 1);
    assert(c > 0); // not allowed: refcount was 0, you can't "revive" it
}

void ta_refcount_dec(struct ta_refcount *rc)
{
    assert(rc);
    rc -= 1;

    unsigned long c = atomic_fetch_add(&rc->count, -1);
    assert(c != 0); // not allowed: refcount was 0
    if (!c) {
        if (rc->on_free) {
            rc->on_free(rc->on_free_ctx, rc->child);
        } else {
            ta_free(rc->child);
        }
        ta_free(rc);
    }
}

// Return whether the refcount is 1. Note that if the result is false, it could
// become true any time in the general case, since other threads may be changing
// the refcount. But if true is returned, it means you are the only owner of the
// refcounted object. (If you are not an owner, it's UB to call this function.)
bool ta_refcount_is_1(struct ta_refcount *rc)
{
    assert(rc);
    rc -= 1;

    return atomic_load(&rc->count) == 1;
}

struct ta_refuser {
    struct ta_refcount *rc;
};

static void autofree_dtor(void *p)
{
    struct ta_refuser *ru = p;

    ta_refcount_dec(ru->rc);
}

// Refcount user helper. This is a ta allocation which on creation increments
// the refcount, and decrements it when it is free'd.
//
// The user must not access the contents of the allocation or resize it. Calling
// ta_set_destructor() is not allowed. But you can freely reparent it or add
// child allocation. ta_free_children() is also allowed and does not affect the
// refcount.
struct ta_refuser *ta_alloc_auto_ref(void *ta_parent, struct ta_refcount *rc)
{
    struct ta_refuser *ru = ta_new(ta_parent, struct ta_refuser);
    if (!ru)
        return NULL;

    ta_set_destructor(ru, autofree_dtor);
    ru->rc = rc;
    ta_refcount_add(ru->rc);
    return ru;
}
