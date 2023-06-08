#include <limits.h>     // for CHAR_BIT
#include <stdatomic.h>
#include "sylvan_bitmap.h"
#include <libpopcnt.h>

enum
{
    BITS_PER_WORD = sizeof(word_t) * CHAR_BIT,
};

/**
 * @brief Return position of the first most significant 1-bit
 */
static inline uint64_t get_first_msb_one_bit_pos(word_t word, size_t word_idx)
{
    return BITS_PER_WORD * word_idx + __builtin_clzll(word);
}

/**
 * @brief Return position of the first least significant 1-bit
 */
static inline uint64_t get_first_lsb_one_bit_pos(word_t word, size_t word_idx)
{
    return BITS_PER_WORD * word_idx + (64 - __builtin_ctzll(word));
}

inline void bitmap_set(word_t *words, size_t pos)
{
    words[WORD_INDEX(pos)] |= BIT_MASK(pos);
}

inline void bitmap_clear(word_t *words, size_t pos)
{
    words[WORD_INDEX(pos)] &= ~BIT_MASK(pos);
}

inline char bitmap_get(const word_t *words, size_t pos)
{
    return words[WORD_INDEX(pos)] & BIT_MASK(pos) ? 1 : 0;
}

inline size_t bitmap_first(word_t *words, size_t size)
{
    return bitmap_first_from(words, 0, size);
}

size_t bitmap_first_from(word_t *words, size_t word_idx, size_t size)
{
    size_t nwords = NUMBER_OF_WORDS(size);
    // find the first word which contains at least one 1-bit
    while (word_idx < nwords && words[word_idx] == 0) word_idx++;
    if (word_idx == nwords) return npos; // no 1-bit found
    else return get_first_msb_one_bit_pos(words[word_idx], word_idx);
}

size_t bitmap_next(word_t *words, size_t size, size_t pos)
{
    if (pos == npos || (pos + 1) >= size) return npos;
    pos++;
    // get word for pos++
    size_t word_idx = WORD_INDEX(pos);
    // check whether there are still any 1-bits in the current word
    word_t word = words[word_idx] & BIT_FWD_ITER_MASK(pos);
    if (word) {
        // there exist some successor 1 bit in the word, thus return the pos directly
        return get_first_msb_one_bit_pos(word, word_idx);
    } else {
        // the current word does not contain any successor 1-bits,
        // thus now find the next word that is not 0 and return the first 1-bit pos
        word_idx++;
        return bitmap_first_from(words, word_idx, size);
    }
}

inline size_t bitmap_last(word_t *words, size_t size)
{
    return bitmap_last_from(words, size);
}

size_t bitmap_last_from(word_t *words, size_t pos)
{
    size_t word_idx = WORD_INDEX(pos);
    if (word_idx == 0) return npos;
    // find the last word which contains at least one 1-bit
    while (word_idx > 0 && words[word_idx] == 0) word_idx--;
    if (word_idx == 0) return npos; // no 1-bit found
    else return get_first_lsb_one_bit_pos(words[word_idx], word_idx);
}

size_t bitmap_prev(word_t *words, size_t pos)
{
    if (pos == 0 || pos == npos) return npos;
    pos--;
    size_t word_idx = WORD_INDEX(pos);
    // check whether there are still any predecessor 1-bits in the current word
    word_t word = words[word_idx] & BIT_BCK_ITER_MASK(pos);
    if (word) {
        // there exist some predecessor 1 bit in the word, thus return the pos directly
        return get_first_lsb_one_bit_pos(word, word_idx);
    } else {
        // the current word does not contain any successor 1-bits,
        // thus now find the next word that is not 0 and return the first 1-bit pos
        word_idx--;
        return bitmap_last_from(words, word_idx);
    }
}

size_t bitmap_count(word_t *words, size_t size)
{
    return popcnt(words, NUMBER_OF_WORDS(size) * 8);
}

inline size_t bitmap_atomic_first(atomic_word_t *words, size_t size)
{
    return bitmap_atomic_first_from(words, 0, size);
}

size_t bitmap_atomic_first_from(atomic_word_t *words, size_t word_idx, size_t size)
{
    size_t nwords = NUMBER_OF_WORDS(size);
    atomic_word_t *ptr = words + word_idx;
    word_t word = atomic_load_explicit(ptr, memory_order_relaxed);
    // find the first word which contains at least one 1-bit
    while (word_idx < nwords && word == 0) {
        word_idx++;
        ptr = words + word_idx;
        word = atomic_load_explicit(ptr, memory_order_relaxed);
    }
    if (word_idx == nwords) {
        // we have reached the end of the bitmap
        return npos;
    } else {
        // we have found the first word which contains at least one 1-bit
        return get_first_msb_one_bit_pos(word, word_idx);
    }
}

size_t bitmap_atomic_next(atomic_word_t *words, size_t size, size_t pos)
{
    if (pos == npos || (pos + 1) >= size) return npos;
    pos++;
    // get word index for pos
    size_t word_idx = WORD_INDEX(pos);
    atomic_word_t *ptr = words + word_idx;
    // check whether there are still any successor 1-bits in the current word
    word_t word = atomic_load_explicit(ptr, memory_order_relaxed) & BIT_FWD_ITER_MASK(pos);
    if (word) {
        // there exist some successor 1 bit in the word, thus return the pos directly
        return get_first_msb_one_bit_pos(word, word_idx);
    } else {
        // the current word does not contain any successor 1-bits,
        // thus now find the next word that is not 0 and return the first 1-bit pos
        word_idx++;
        return bitmap_atomic_first_from(words, word_idx, size);
    }
}

inline size_t bitmap_atomic_last(atomic_word_t *words, size_t size)
{
    return bitmap_atomic_last_from(words, size);
}

size_t bitmap_atomic_last_from(atomic_word_t *words, size_t pos)
{
    size_t word_idx = WORD_INDEX(pos);
    if (word_idx == 0 || word_idx == npos) return npos;
    atomic_word_t *ptr = words + word_idx;
    word_t word = atomic_load_explicit(ptr, memory_order_relaxed);
    // find the last word which contains at least one 1-bit
    while (word_idx > 0 && word == 0) {
        word_idx--;
        ptr = words + word_idx;
        word = atomic_load_explicit(ptr, memory_order_relaxed);
    }
    if (word_idx == 0) {
        // we have reached the end of the bitmap
        return npos;
    } else {
        // we have found the first word which contains at least one 1-bit
        return get_first_lsb_one_bit_pos(word, word_idx);
    }
}

size_t bitmap_atomic_prev(atomic_word_t *words, size_t pos)
{
    if (pos == 0 || pos == npos) return npos;
    pos--;
    size_t word_idx = WORD_INDEX(pos);
    atomic_word_t *ptr = words + word_idx;
    // check whether there are still any predecessor 1-bits in the current word
    word_t word = atomic_load_explicit(ptr, memory_order_acquire) & BIT_BCK_ITER_MASK(pos);
    if (word) {
        // there exist some predecessor 1 bit in the word, thus return the pos directly
        return get_first_lsb_one_bit_pos(word, word_idx);
    } else {
        // the current word does not contain any predecessor 1-bits,
        // thus now find the next word that is not 0 and return the first 1-bit lsb pos
        word_idx--;
        return bitmap_atomic_last_from(words, word_idx);
    }
}

int bitmap_atomic_set(atomic_word_t *words, size_t pos)
{
    atomic_word_t *ptr = words + WORD_INDEX(pos);
    uint64_t v = atomic_load_explicit(ptr, memory_order_acquire);
    word_t mask = BIT_MASK(pos);
    if (v & mask) return 0;
    atomic_fetch_or_explicit(ptr, mask, memory_order_release);
    return 1;
}

int bitmap_atomic_clear(atomic_word_t *words, size_t pos)
{
    atomic_word_t *ptr = words + WORD_INDEX(pos);
    uint64_t v = atomic_load_explicit(ptr, memory_order_acquire);
    word_t mask = BIT_MASK(pos);
    if ((v & mask) == 0) return 0;
    atomic_fetch_and_explicit(ptr, ~mask, memory_order_release);
    return 1;
}

int bitmap_atomic_get(atomic_word_t *words, size_t pos)
{
    (void)pos;
    (void)words;
    atomic_word_t *ptr = words + WORD_INDEX(pos);
    uint64_t word = atomic_load_explicit(ptr, memory_order_relaxed);
    return word & BIT_MASK(pos) ? 1 : 0;
}