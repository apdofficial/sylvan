#include <stdio.h>
#include "test_assert.h"
#include "sylvan_int.h"
#include "sylvan_levels.h"
#include "common.h"
#include <sylvan_align.h>

int main()
{
    size_t size = 90000;
    word_t *bitmap = (word_t *) alloc_aligned(size);

    // set bits between 5 to 9 to 1
    for (size_t i = 5; i < 10000; i++) {
        bitmap_set(bitmap, i);
    }

    for (size_t i = 5; i < 10000; i++) {
        assert(bitmap_get(bitmap, i));
    }

    test_assert(bitmap_first(bitmap, size) == 5);
//    test_assert(bitmap_last(bitmap, size) == 9);
//    test_assert(bitmap_count(bitmap, size) == 5);

    size_t i = 5;
    size_t index = bitmap_first(bitmap, size);
    while (index != npos) {
        test_assert(index == i);
        index = bitmap_next(bitmap, size, index);
        i++;
    }

//    index = bitmap_last(bitmap, size);
//    while (index != npos) {
//        test_assert(index == i);
//        index = bitmap_prev(bitmap, index);
//        i--;
//    }

    free_aligned(bitmap, size);

    atomic_word_t *atomic_bitmap = (atomic_word_t *) alloc_aligned(size);

    // set bits between 5 to 9 to 1
    for (size_t i = 5; i < 10000; i++) {
        bitmap_atomic_set(atomic_bitmap, i);
    }

    for (size_t i = 5; i < 10000; i++) {
        assert(bitmap_atomic_get(atomic_bitmap, i));
    }

    test_assert(bitmap_atomic_first(atomic_bitmap, size) == 5);
//    test_assert(bitmap_last(bitmap, size) == 9);

    i = 5;
    index = bitmap_atomic_first(atomic_bitmap, size);
    while (index != npos) {
        test_assert(index == i);
        index = bitmap_atomic_next(atomic_bitmap, size, index);
        i++;
    }

    free_aligned(atomic_bitmap, size);

    return 0;
}