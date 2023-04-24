#ifndef SYLVAN_BITMAP_H
#define SYLVAN_BITMAP_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <stddef.h>

// use uint64_t to advantage the usual 64 bytes per cache line
typedef uint64_t word_t;
typedef _Atomic (word_t) atomic_word_t;

static const size_t npos = (word_t)-1;

#define USE_LLMSET 0

#define WORD_OFFSET(b) ((b) / BITS_PER_WORD)
#define BIT_OFFSET(b) ((b) % BITS_PER_WORD)

#if USE_LLMSET
#define BIT_MASK(b) ((word_t) 0 << BIT_OFFSET(b))
#else
#define BIT_MASK(b) ((word_t)0x8000000000000000LL >> (b&63)) // sift the most significant bit to the right by b&63 (we only have 64 )
#endif

#define BIT_OFFSET(b) ((b) % BITS_PER_WORD)
#define NUMBER_OF_WORDS(b) ((b + BITS_PER_WORD-1) / BITS_PER_WORD)

/**
 * Set the bit at position n to 1, if it was 0.
 */
void bitmap_set(word_t *words, size_t pos);

/**
 * Set the bit at position n to 0, if it was 1.
 */
void bitmap_clear(word_t *words, size_t pos);

/**
 * Get the bit at position n.
 */
char bitmap_get(word_t *words, size_t pos);

/**
 * Get the first bit set to 1
 */
size_t bitmap_first(word_t *words, size_t size);

size_t bitmap_first_from(word_t *words,size_t starting_word, size_t size);

/**
 * Get the first bit set to 1 (atomic version)
 */
size_t bitmap_atomic_first(atomic_word_t *words, size_t size);

size_t bitmap_atomic_first_from(atomic_word_t *words, size_t starting_word, size_t size);

/**
 * Get the last bit set to 1
 */
size_t bitmap_last(word_t *words, size_t size);

/**
 * Get the next bit set to 1
 */
size_t bitmap_next(word_t *words, size_t size, size_t pos);


/**
 * Get the next bit set to 1 (atomic version)
 */
size_t bitmap_atomic_next(atomic_word_t *words, size_t size, size_t pos);

/**
 * Get the previous bit set to 1
 */
size_t bitmap_prev(word_t *words, size_t pos);

/**
 * Count the number of bits set to 1
 */
size_t bitmap_count(word_t *words, size_t size);

uint64_t bitmap_atomic_load(atomic_word_t *words, size_t pos);

/**
 * Set the bit at position n to 1, if it was 0. (Atomic version)
 */
void bitmap_atomic_set(atomic_word_t *words, size_t pos);

/**
 * Set the bit at position n to 0, if it was 1. (Atomic version)
 */
void bitmap_atomic_clear(atomic_word_t *words, size_t pos);

/**
 * Get the bit at position n. (Atomic version)
 */
int bitmap_atomic_get(atomic_word_t *words, size_t pos);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif // SYLVAN_BITMAP_H