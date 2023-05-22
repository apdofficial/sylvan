#include <stdio.h>
#include "test_assert.h"
#include "sylvan_int.h"
#include "sylvan_levels.h"
#include "common.h"
#include <sylvan_align.h>

int test_forward_iterator(size_t i, size_t j, size_t size)
{
    word_t *bitmap = (word_t *) alloc_aligned(size);

    for (size_t k = i; k < j; k++) {
        bitmap_set(bitmap, k);
    }

    for (size_t k = i; k < j; k++) {
        assert(bitmap_get(bitmap, k));
    }

    test_assert(bitmap_first(bitmap, size) == i);

    size_t k = i;
    size_t index = bitmap_first(bitmap, size);
    while (index != npos) {
        test_assert(index == k);
        index = bitmap_next(bitmap, size, index);
        k++;
    }

    test_assert(bitmap_count(bitmap, size) == j - i);

    free_aligned(bitmap, size);

    return 0;
}

int test_backwards_iterator(size_t i, size_t j, size_t size)
{
    word_t *bitmap = (word_t *) alloc_aligned(size);

    for (size_t k = i; k < j; k++) {
        bitmap_set(bitmap, k);
    }

    for (size_t k = i; k < j; k++) {
        assert(bitmap_get(bitmap, k));
    }

    test_assert(bitmap_last(bitmap, size - 1) == j);

    size_t k = j;
    size_t index = bitmap_last(bitmap, size - 1);
    while (index != npos) {
        test_assert(index == k);
        index = bitmap_prev(bitmap, index);
        k--;
    }

    test_assert(bitmap_count(bitmap, size) == j - i);

    free_aligned(bitmap, size);

    return 0;
}

int test_atomic_forward_iterator(size_t i, size_t j, size_t size)
{
    atomic_word_t *bitmap = (atomic_word_t *) alloc_aligned(size);

    for (size_t k = i; k < j; k++) {
        bitmap_atomic_set(bitmap, k);
    }

    for (size_t k = i; k < j; k++) {
        assert(bitmap_atomic_get(bitmap, k));
    }

    test_assert(bitmap_atomic_first(bitmap, size) == i);

    size_t k = i;
    size_t index = bitmap_atomic_first(bitmap, size);
    while (index != npos) {
        test_assert(index == k);
        index = bitmap_atomic_next(bitmap, size, index);
        k++;
    }

    free_aligned(bitmap, size);

    return 0;
}

int test_atomic_backwards_iterator(size_t i, size_t j, size_t size)
{
    atomic_word_t *bitmap = (atomic_word_t *) alloc_aligned(size);

    for (size_t k = i; k < j; k++) {
        bitmap_atomic_set(bitmap, k);
    }

    for (size_t k = i; k < j; k++) {
        assert(bitmap_atomic_get(bitmap, k));
    }

    test_assert(bitmap_atomic_last(bitmap, size - 1) == j);

    size_t k = j;
    size_t index = bitmap_atomic_last(bitmap, size - 1);
    while (index != npos) {
        test_assert(index == k);
        index = bitmap_atomic_prev(bitmap, index);
        k--;
    }

    free_aligned(bitmap, size);

    return 0;
}

static inline size_t _rand()
{
    return rand() % 7919; // some not small prime number
}

int runtests(size_t ntests)
{
    printf("test_forward_iterator\n");
    for (size_t j = 0; j < ntests; j++) {
        size_t i = _rand();
        size_t j = i + _rand();
        size_t size = j + 10;
        if (test_forward_iterator(i, j, size)) return 1;
    }
    printf("test_backwards_iterator\n");
    for (size_t j = 0; j < ntests; j++) {
        size_t i = _rand();
        size_t j = i + _rand();
        size_t size = j + 10;
        if (test_backwards_iterator(i, j, size)) return 1;
    }
    printf("test_atomic_forward_iterator\n");
    for (size_t j = 0; j < ntests; j++) {
        size_t i = _rand();
        size_t j = i + _rand();
        size_t size = j + 10;
        if (test_atomic_forward_iterator(i, j, size)) return 1;
    }
    printf("test_atomic_backwards_iterator\n");
    for (size_t j = 0; j < ntests; j++) {
        size_t i = _rand();
        size_t j = i + _rand();
        size_t size = j + 10;
        if (test_atomic_backwards_iterator(i, j, size)) return 1;
    }
    return 0;
}

int main()
{
    return runtests(100);
}