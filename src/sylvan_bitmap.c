#include <limits.h>     // for CHAR_BIT
#include <stdint.h>     // for uint32_t
#include <stdatomic.h>
#include "sylvan_bitmap.h"

#include <libpopcnt.h>

enum
{
    BITS_PER_WORD = sizeof(word_t) * CHAR_BIT,
};

static inline int bsr(uint64_t x)
{
    return __builtin_clzll(x) ^ (BITS_PER_WORD-1);
}

#define WORD_OFFSET(b) ((b) / BITS_PER_WORD)
#define BIT_OFFSET(b) ((b) % BITS_PER_WORD)
#define NUMBER_OF_WORDS(b) ((b + BITS_PER_WORD-1) / BITS_PER_WORD)

void bitmap_set(word_t *words, size_t pos)
{
    words[WORD_OFFSET(pos)] |= ((word_t) 1 << BIT_OFFSET(pos));
}

void bitmap_clear(word_t *words, size_t pos)
{
    words[WORD_OFFSET(pos)] &= ~((word_t) 1 << BIT_OFFSET(pos));
}

char bitmap_get(word_t *words, size_t pos)
{
    word_t bit = words[WORD_OFFSET(pos)] & ((word_t) 1 << BIT_OFFSET(pos));
    return bit != 0;
}

size_t bitmap_first(word_t *words, size_t size)
{
    size_t nwords = NUMBER_OF_WORDS(size);
    size_t i = 0;
    while (i < nwords && words[i] == 0) i++;
    if (i == nwords) return npos;
    else return i * BITS_PER_WORD + __builtin_ffsll(words[i]) - 1;
}

size_t bitmap_last(word_t *words, size_t size)
{
    size_t nwords = NUMBER_OF_WORDS(size);
    if (nwords == 0) return npos;
    size_t i = nwords - 1;
    for (;;) {
        if (words[i] != 0) return i * BITS_PER_WORD + bsr(words[i]);
        if (i == 0) return npos;
        i--;
    }
}

size_t bitmap_next(word_t *words, size_t size, size_t pos)
{
    size_t nwords = NUMBER_OF_WORDS(size);
    if (pos == npos || (pos + 1) >= size) return npos;
    pos++;
    size_t i = WORD_OFFSET(pos);
    uint64_t m = words[i] & (~((uint64_t) 0) << BIT_OFFSET(pos));
    if (m) {
        return i * BITS_PER_WORD + __builtin_ffsll(m) - 1;
    } else {
        i += 1;
        while (i < nwords && words[i] == 0) i++;
        if (i == nwords) return npos;
        else return i * BITS_PER_WORD + __builtin_ffsll(words[i]) - 1;
    }
}

size_t bitmap_prev(word_t *words, size_t pos)
{
    if (pos == 0 || pos == npos) return npos;
    size_t i = WORD_OFFSET(pos);
    uint64_t m = words[i] & ~((~((uint64_t) 0)) << (int) BIT_OFFSET(pos));
    if (m) {
        return i * BITS_PER_WORD + bsr(m);
    } else {
        if (i == 0) return npos;
        i -= 1;
        for (;;) {
            if (words[i] != 0) return i * BITS_PER_WORD + bsr(words[i]);
            if (i == 0) return npos;
            i--;
        }
    }
}

size_t bitmap_count(word_t *words, size_t size)
{
    return popcnt(words, NUMBER_OF_WORDS(size)*8);
}

void bitmap_atomic_set(atomic_word_t *words, size_t pos)
{
    atomic_word_t *word = &words[WORD_OFFSET(pos)];
    uint64_t old_v = atomic_load_explicit(word, memory_order_relaxed);
    uint64_t new_v = old_v | ((word_t) 1 << BIT_OFFSET(pos));
    atomic_compare_exchange_strong(word, &old_v, new_v);
}

void bitmap_atomic_clear(atomic_word_t *words, size_t pos)
{
    atomic_word_t *word = &words[WORD_OFFSET(pos)];
    uint64_t old_v = atomic_load_explicit(word, memory_order_relaxed);
    uint64_t new_v = old_v & ~((word_t) 1 << BIT_OFFSET(pos));
    atomic_compare_exchange_strong(word, &old_v, new_v);
}

int bitmap_atomic_get(atomic_word_t *words, size_t pos)
{
    atomic_word_t *word = &words[WORD_OFFSET(pos)];
    uint64_t v = atomic_load_explicit(word, memory_order_relaxed);
    word_t bit = v & ((word_t) 1 << BIT_OFFSET(pos));
    return bit != 0;
}