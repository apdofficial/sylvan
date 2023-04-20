#include <stddef.h>

// With 64 bytes per cache line, there are 8 64-bit values per cache line.
typedef uint64_t word_t;
typedef _Atomic (word_t) atomic_word_t;

/**
 * Set the bit at position n to 1, if it was 0.
 */
void bitmap_set(word_t *words, int n);

/**
 * Set the bit at position n to 0, if it was 1.
 */
void bitmap_clear(word_t *words, int n);

/**
 * Get the bit at position n.
 */
int bitmap_get(word_t *words, int n);

/**
 * Set the bit at position n to 1, if it was 0. (Atomic version)
 */
void bitmap_atomic_set(_Atomic (word_t) *words, int n);

/**
 * Set the bit at position n to 0, if it was 1. (Atomic version)
 */
void bitmap_atomic_clear(_Atomic (word_t) *words, int n);

/**
 * Get the bit at position n. (Atomic version)
 */
int bitmap_atomic_get(_Atomic (word_t) *words, int n);