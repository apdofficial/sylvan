#ifndef SYLVAN_BITMAP_H
#define SYLVAN_BITMAP_H

#include <stddef.h>
#include <stdint.h>
#include <limits.h>     // for CHAR_BIT

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

// use uint64_t/ uint32_t to advantage the usual 64 bytes per cache line
typedef uint64_t bitmap_container_t;

typedef struct bitmap_s {
    bitmap_container_t *container;
    size_t size;
} bitmap_t;

typedef struct atomic_bitmap_s {
    _Atomic (bitmap_container_t) *container;
    size_t size;
} atomic_bitmap_t;

enum
{
    BITS_PER_WORD = sizeof(bitmap_container_t) * CHAR_BIT,
};

static const size_t npos = (uint64_t)-1;

#define WORD_INDEX(b)           ((b) / BITS_PER_WORD)
#define BIT_OFFSET(b)           ((b) % BITS_PER_WORD)
#define BIT_MASK(b)             ((uint64_t) 0x8000000000000000LL >> (b))
#define BIT_FWD_ITER_MASK(b)    (~((uint64_t) 0x0) >> (b))
#define BIT_BCK_ITER_MASK(b)    (~(~((uint64_t) 0x0) >> (int)(b)))
#define NUMBER_OF_WORDS(b)      (((b) + BITS_PER_WORD-1) / BITS_PER_WORD)

/*
 * Allocate a new bitmap with the given size
 */
void bitmap_init(bitmap_t* bitmap, size_t new_size);

/*
 * Free the bitmap
 */
void bitmap_deinit(bitmap_t *bitmap);

/**
 * Set the bit at position n to 1, if it was 0.
 */
void bitmap_set(bitmap_t *bitmap, size_t pos);

/**
 * Set the bit at position n to 0, if it was 1.
 */
void bitmap_clear(bitmap_t *bitmap, size_t pos);

/**
 * Get the bit at position n.
 */
char bitmap_get(const bitmap_t *bitmap, size_t pos);

/**
 * Get the first bit set to 1
 */
size_t bitmap_first(bitmap_t *bitmap);

/**
 * Get the first bit set to 1 (atomic version)
 */
size_t bitmap_first_from(bitmap_t *bitmap, size_t starting_word);

/**
 * Get the last bit set to 1
 */
size_t bitmap_last(bitmap_t *bitmap, size_t size);

/**
 * Get the last 1-bit position from the given word index
 */
size_t bitmap_last_from(bitmap_t *bitmap, size_t pos);

/**
 * Get the next bit set to 1
 */
size_t bitmap_next(bitmap_t *bitmap, size_t size, size_t pos);

/**
 * Get the previous bit set to 1
 */
size_t bitmap_prev(bitmap_t *bitmap, size_t pos);

/**
 * Count the number of bits set to 1
 */
size_t bitmap_count(bitmap_t *bitmap);

/*
 * Allocate a new bitmap with the given size
 */
void atomic_bitmap_init(atomic_bitmap_t* bitmap, size_t new_size);

/*
 * Free the bitmap
 */
void atomic_bitmap_deinit(atomic_bitmap_t *bitmap);

/**
 * Set all bits to 0
 */
void atomic_bitmap_clear_all(atomic_bitmap_t *bitmap);

/**
 * Get the first bit set to 1 (atomic version)
 */
size_t atomic_bitmap_first(atomic_bitmap_t *bitmap);

/**
 * Get the first 1-bit position from the given word index (atomic version)
 */
size_t atomic_bitmap_first_from(atomic_bitmap_t *bitmap, size_t word_idx);

/**
 * Get the last bit set to 1
 */
size_t atomic_bitmap_last(atomic_bitmap_t *bitmap);

/*
 * Get the last 1-bit position from the given word index (atomic version)
 */
size_t atomic_bitmap_last_from(atomic_bitmap_t *bitmap, size_t pos);

/**
 * Get the next bit set to 1 (atomic version)
 */
size_t atomic_bitmap_next(atomic_bitmap_t *bitmap, size_t pos);

/**
 * Get the previous bit set to 1
 */
size_t atomic_bitmap_prev(atomic_bitmap_t *bitmap, size_t pos);

/**
 * Set the bit at position n to 1, if it was 0. (Atomic version)
 */
int atomic_bitmap_set(atomic_bitmap_t *bitmap, size_t pos);

/**
 * Set the bit at position n to 0, if it was 1. (Atomic version)
 */
int atomic_bitmap_clear(atomic_bitmap_t *bitmap, size_t pos);

/**
 * Get the bit at position n. (Atomic version)
 */
int atomic_bitmap_get(atomic_bitmap_t *bitmap, size_t pos);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif // SYLVAN_BITMAP_H