#include <stddef.h>

// use uint64_t to advantage the usual 64 bytes per cache line
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
void bitmap_atomic_set(atomic_word_t *words, int n);

/**
 * Set the bit at position n to 0, if it was 1. (Atomic version)
 */
void bitmap_atomic_clear(atomic_word_t *words, int n);

/**
 * Get the bit at position n. (Atomic version)
 */
int bitmap_atomic_get(atomic_word_t *words, int n);