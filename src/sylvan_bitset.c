#include <limits.h>     // for CHAR_BIT
#include <stdint.h>     // for uint32_t
#include <stdatomic.h>
#include "sylvan_bitset.h"

enum
{
    BITS_PER_WORD = sizeof(word_t) * CHAR_BIT
};
#define WORD_OFFSET(b) ((b) / BITS_PER_WORD)
#define BIT_OFFSET(b)  ((b) % BITS_PER_WORD)

void bitmap_set(word_t *words, int n)
{
    words[WORD_OFFSET(n)] |= ((word_t) 1 << BIT_OFFSET(n));
}

void bitmap_clear(word_t *words, int n)
{
    words[WORD_OFFSET(n)] &= ~((word_t) 1 << BIT_OFFSET(n));
}

int bitmap_get(word_t *words, int n)
{
    word_t bit = words[WORD_OFFSET(n)] & ((word_t) 1 << BIT_OFFSET(n));
    return bit != 0;
}

void bitmap_atomic_set(atomic_word_t *words, int n)
{
    atomic_word_t *word = &words[WORD_OFFSET(n)];
    uint64_t old_v = atomic_load_explicit(word, memory_order_relaxed);
    uint64_t new_v = old_v | ((word_t) 1 << BIT_OFFSET(n));
    atomic_compare_exchange_strong(word, &old_v, new_v);
}

void bitmap_atomic_clear(atomic_word_t *words, int n)
{
    atomic_word_t *word = &words[WORD_OFFSET(n)];
    uint64_t old_v = atomic_load_explicit(word, memory_order_relaxed);
    uint64_t new_v = old_v & ~((word_t) 1 << BIT_OFFSET(n));
    atomic_compare_exchange_strong(word, &old_v, new_v);
}

int bitmap_atomic_get(atomic_word_t *words, int n)
{
    atomic_word_t *word = &words[WORD_OFFSET(n)];
    uint64_t v = atomic_load_explicit(word, memory_order_relaxed);
    word_t bit = v & ((word_t) 1 << BIT_OFFSET(n));
    return bit != 0;
}

