#include <limits.h>     // for CHAR_BIT
#include <stdint.h>     // for uint32_t
#include <stdatomic.h>
#include "sylvan_bitset.h"
#include <stdio.h>
#include <libpopcnt.h>

enum
{
    BITS_PER_WORD = sizeof(word_t) * CHAR_BIT,
};

static inline uint64_t get_bit_pos(word_t word, size_t word_idx)
{
    return BITS_PER_WORD * word_idx + __builtin_clzll(word);
}

// __builtin_ffs:
// Returns one plus the index of the least significant 1-bit of x, or
// if x is zero, returns zero.

// Bit Search Reverse
static inline int bsr(uint64_t x)
{
    // __builtin_clzll:
    // Returns the number of leading 0-bits in x, starting at the most significant bit position.
    // If x is 0, the result is undefined.
    return __builtin_clzll(x) ^ (BITS_PER_WORD-1);
}

void bitmap_set(word_t *words, size_t pos)
{
    words[WORD_OFFSET(pos)] |= BIT_MASK(pos);
}

void bitmap_clear(word_t *words, size_t pos)
{
    words[WORD_OFFSET(pos)] &= ~BIT_MASK(pos);
}

char bitmap_get(word_t *words, size_t pos)
{
    return words[WORD_OFFSET(pos)] & BIT_MASK(pos) ? 1 : 0;
}

size_t bitmap_first(word_t *words, size_t size)
{
    return bitmap_first_from(words, 0, size);
}

size_t bitmap_atomic_first(atomic_word_t *words, size_t size)
{
    return bitmap_atomic_first_from(words, 0, size);
}

size_t bitmap_atomic_first_from(atomic_word_t *words, size_t starting_word, size_t size)
{
    size_t nwords = NUMBER_OF_WORDS(size);
    size_t word_idx = starting_word;
    while (word_idx < nwords && bitmap_atomic_load(words, word_idx) == 0) word_idx++;
    if (word_idx == nwords) return npos;
    else return get_bit_pos(bitmap_atomic_load(words, word_idx), word_idx);
}

size_t bitmap_first_from(word_t *words, size_t word_idx, size_t size)
{
    size_t nwords = NUMBER_OF_WORDS(size);
    while (word_idx < nwords && words[word_idx] == 0) word_idx++;
    if (word_idx == nwords) return npos;
    else return get_bit_pos(words[word_idx], word_idx);
}

size_t bitmap_next(word_t *words, size_t size, size_t pos)
{
    if (pos == npos || (pos + 1) >= size) return npos;
    pos++;
    // get word for pos++
    size_t word_idx = WORD_OFFSET(pos);
    uint64_t word = words[word_idx] & (pos & 63);
    // if there exist any 1 bit in the word then return the pos directly
    if (word) return get_bit_pos(word, word_idx);
    // the current word is 0, then find the next word that is not 0 and return the first 1 bit pos
    else return bitmap_first_from(words, ++word_idx,  size);
}

size_t bitmap_atomic_next(atomic_word_t *words, size_t size, size_t pos)
{
    if (pos == npos || (pos + 1) >= size) return npos;
    pos++;
    // get word for pos++
    size_t word_idx = WORD_OFFSET(pos);
    uint64_t word = bitmap_atomic_load(words, word_idx) & (pos & 63);
    // if there exist any 1 bit in the word then return the pos directly
    if (word) return get_bit_pos(word, word_idx);
    // the current word is 0, then find the next word that is not 0 and return the first 1 bit pos
    else return bitmap_atomic_first_from(words, ++word_idx,  size);
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

uint64_t bitmap_atomic_load(atomic_word_t *words, size_t pos)
{
    atomic_word_t *word = &words[WORD_OFFSET(pos)];
    return atomic_load_explicit(word, memory_order_relaxed);
}

void bitmap_atomic_set(atomic_word_t *words, size_t pos)
{
    atomic_word_t *word = &words[WORD_OFFSET(pos)];
    uint64_t old_v = atomic_load_explicit(word, memory_order_relaxed);
    uint64_t new_v = old_v | BIT_MASK(pos);
    atomic_compare_exchange_strong(word, &old_v, new_v);
}

void bitmap_atomic_clear(atomic_word_t *words, size_t pos)
{
    atomic_word_t *word = &words[WORD_OFFSET(pos)];
    uint64_t old_v = atomic_load_explicit(word, memory_order_relaxed);
    uint64_t new_v = old_v & ~BIT_MASK(pos);
    atomic_compare_exchange_strong(word, &old_v, new_v);
}

int bitmap_atomic_get(atomic_word_t *words, size_t pos)
{
    atomic_word_t *word = &words[WORD_OFFSET(pos)];
    uint64_t v = atomic_load_explicit(word, memory_order_relaxed);
    word_t bit = v & BIT_MASK(pos);
    return bit != 0;
}