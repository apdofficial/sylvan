#include <sylvan_bitmap.h>
#include <lace.h>
#include <sylvan_align.h>

#include <stdatomic.h>
#include <libpopcnt.h>
#include <assert.h>

/**
 * @brief Return position of the first most significant 1-bit
 */
static inline bitmap_container_t get_first_msb_one_bit_pos(bitmap_container_t container, size_t word_idx)
{
    return BITS_PER_WORD * word_idx + __builtin_clzll(container);
}

/**
 * @brief Return position of the first least significant 1-bit
 */
static inline bitmap_container_t get_first_lsb_one_bit_pos(bitmap_container_t container,  size_t word_idx)
{
    return BITS_PER_WORD * word_idx + (64 - __builtin_ctzll(container));
}

void bitmap_init(bitmap_t* bitmap, size_t new_size)
{
    bitmap_deinit(bitmap);
    bitmap->container = (bitmap_container_t *) alloc_aligned(new_size);
    if (bitmap != NULL) bitmap->size = new_size;
    else bitmap->size = 0;
}


void bitmap_deinit(bitmap_t *bitmap)
{
    if (bitmap->container != NULL) free_aligned(bitmap->container, bitmap->size);
    bitmap->size = 0;
    bitmap->container = NULL;
}

inline void bitmap_set(bitmap_t *bitmap, size_t pos)
{
    bitmap->container[WORD_INDEX(pos)] |= BIT_MASK(pos);
}

inline void bitmap_clear(bitmap_t *bitmap, size_t pos)
{
    bitmap->container[WORD_INDEX(pos)] &= ~BIT_MASK(pos);
}

inline char bitmap_get(const bitmap_t *bitmap, size_t pos)
{
    return bitmap->container[WORD_INDEX(pos)] & BIT_MASK(pos) ? 1 : 0;
}

inline void bitmap_clear_all(bitmap_t *bitmap)
{
    if (bitmap->container == NULL) {
        bitmap->size = 0;
        return;
    }
    if (bitmap->size == 0) return;
    clear_aligned(bitmap->container, bitmap->size);
}

inline size_t bitmap_first(bitmap_t *bitmap)
{
    return bitmap_first_from(bitmap, 0);
}

size_t bitmap_first_from(bitmap_t *bitmap, size_t word_idx)
{
    size_t nwords = NUMBER_OF_WORDS(bitmap->size);
    // find the first word which contains at least one 1-bit
    while (word_idx < nwords && bitmap->container[word_idx] == 0) word_idx++;
    if (word_idx == nwords) return npos; // no 1-bit found
    else return get_first_msb_one_bit_pos(bitmap->container[word_idx], word_idx);
}

size_t bitmap_next(bitmap_t *bitmap,size_t pos)
{
    if (pos == npos || (pos + 1) >= bitmap->size) return npos;
    pos++;
    // get word for pos++
    size_t word_idx = WORD_INDEX(pos);
    // check whether there are still any 1-bits in the current word
    bitmap_container_t word = bitmap->container[word_idx] & BIT_FWD_ITER_MASK(pos);
    if (word) {
        // there exist some successor 1 bit in the word, thus return the pos directly
        return get_first_msb_one_bit_pos(word, word_idx);
    } else {
        // the current word does not contain any successor 1-bits,
        // thus now find the next word that is not 0 and return the first 1-bit pos
        word_idx++;
        return bitmap_first_from(bitmap, word_idx);
    }
}

inline size_t bitmap_last(bitmap_t *bitmap)
{
    return bitmap_last_from(bitmap, bitmap->size - 1);
}

size_t bitmap_last_from(bitmap_t *bitmap, size_t pos)
{
    size_t word_idx = WORD_INDEX(pos);
    if (word_idx == 0) return npos;
    // find the last word which contains at least one 1-bit
    while (word_idx > 0 && bitmap->container[word_idx] == 0) word_idx--;
    if (word_idx == 0) return npos; // no 1-bit found
    else return get_first_lsb_one_bit_pos(bitmap->container[word_idx], word_idx);
}

size_t bitmap_prev(bitmap_t *bitmap, size_t pos)
{
    if (pos == 0 || pos == npos) return npos;
    pos--;
    size_t word_idx = WORD_INDEX(pos);
    // check whether there are still any predecessor 1-bits in the current word
    bitmap_container_t word = bitmap->container[word_idx] & BIT_BCK_ITER_MASK(pos);
    if (word) {
        // there exist some predecessor 1 bit in the word, thus return the pos directly
        return get_first_lsb_one_bit_pos(word, word_idx);
    } else {
        // the current word does not contain any successor 1-bits,
        // thus now find the next word that is not 0 and return the first 1-bit pos
        word_idx--;
        return bitmap_last_from(bitmap, word_idx);
    }
}

size_t bitmap_count(bitmap_t *bitmap)
{
    return popcnt(bitmap->container, NUMBER_OF_WORDS(bitmap->size) * 8);
}

inline size_t atomic_bitmap_first(atomic_bitmap_t *bitmap)
{
    return atomic_bitmap_first_from(bitmap, 0);
}

void atomic_bitmap_init(atomic_bitmap_t* bitmap, size_t new_size)
{
    atomic_bitmap_deinit(bitmap);
    bitmap->container = (_Atomic(bitmap_container_t) *) alloc_aligned(new_size);
    if (bitmap != NULL) bitmap->size = new_size;
    else bitmap->size = 0;
}


void atomic_bitmap_deinit(atomic_bitmap_t *bitmap)
{
    if (bitmap->container != NULL && bitmap->size > 0) free_aligned(bitmap->container, bitmap->size);
    bitmap->size = 0;
    bitmap->container = NULL;
}

void atomic_bitmap_clear_all(atomic_bitmap_t *bitmap)
{
    if (bitmap->container == NULL) {
        bitmap->size = 0;
        return;
    }
    if (bitmap->size == 0) return;
    clear_aligned(bitmap->container, bitmap->size);
}

size_t atomic_bitmap_first_from(atomic_bitmap_t *bitmap, size_t word_idx)
{
    size_t nwords = NUMBER_OF_WORDS(bitmap->size);
    _Atomic(bitmap_container_t) *ptr = bitmap->container + word_idx;
    bitmap_container_t word = atomic_load_explicit(ptr, memory_order_relaxed);
    // find the first word which contains at least one 1-bit
    while (word_idx < nwords && word == 0) {
        word_idx++;
        ptr = bitmap->container + word_idx;
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

size_t atomic_bitmap_next(atomic_bitmap_t *bitmap, size_t pos)
{
    if (pos == npos || (pos + 1) >= bitmap->size) return npos;
    pos++;
    // get word index for pos
    size_t word_idx = WORD_INDEX(pos);
    _Atomic(bitmap_container_t) *ptr = bitmap->container + word_idx;
    // check whether there are still any successor 1-bits in the current word
    bitmap_container_t word = atomic_load_explicit(ptr, memory_order_relaxed) & BIT_FWD_ITER_MASK(pos);
    if (word) {
        // there exist some successor 1 bit in the word, thus return the pos directly
        return get_first_msb_one_bit_pos(word, word_idx);
    } else {
        // the current word does not contain any successor 1-bits,
        // thus now find the next word that is not 0 and return the first 1-bit pos
        word_idx++;
        return atomic_bitmap_first_from(bitmap, word_idx);
    }
}

inline size_t atomic_bitmap_last(atomic_bitmap_t *bitmap)
{
    return atomic_bitmap_last_from(bitmap, bitmap->size - 1);
}

size_t atomic_bitmap_last_from(atomic_bitmap_t *bitmap, size_t pos)
{
    size_t word_idx = WORD_INDEX(pos);
    if (word_idx == 0 || word_idx == npos) return npos;
    _Atomic(bitmap_container_t) *ptr = bitmap->container + word_idx;
    bitmap_container_t word = atomic_load_explicit(ptr, memory_order_relaxed);
    // find the last word which contains at least one 1-bit
    while (word_idx > 0 && word == 0) {
        word_idx--;
        ptr = bitmap->container + word_idx;
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

size_t atomic_bitmap_prev(atomic_bitmap_t *bitmap, size_t pos)
{
    if (pos == 0 || pos == npos) return npos;
    pos--;
    size_t word_idx = WORD_INDEX(pos);
    _Atomic(bitmap_container_t) *ptr = bitmap->container + word_idx;
    // check whether there are still any predecessor 1-bits in the current word
    bitmap_container_t word = atomic_load_explicit(ptr, memory_order_relaxed) & BIT_BCK_ITER_MASK(pos);
    if (word) {
        // there exist some predecessor 1 bit in the word, thus return the pos directly
        return get_first_lsb_one_bit_pos(word, word_idx);
    } else {
        // the current word does not contain any predecessor 1-bits,
        // thus now find the next word that is not 0 and return the first 1-bit lsb pos
        word_idx--;
        return atomic_bitmap_last_from(bitmap, word_idx);
    }
}

int atomic_bitmap_set(atomic_bitmap_t *bitmap, size_t pos)
{
    assert(pos < bitmap->size);
    _Atomic(bitmap_container_t) *ptr = bitmap->container + WORD_INDEX(pos);
    uint64_t mask = BIT_MASK(pos);
    atomic_fetch_or_explicit(ptr, mask, memory_order_release);
    return 1;
}

int atomic_bitmap_clear(atomic_bitmap_t *bitmap, size_t pos)
{
    assert(pos < bitmap->size);
    _Atomic(bitmap_container_t) *ptr = bitmap->container + WORD_INDEX(pos);
    uint64_t mask = BIT_MASK(pos);
    atomic_fetch_and_explicit(ptr, ~mask, memory_order_release);
    return 1;
}

int atomic_bitmap_get(const atomic_bitmap_t *bitmap, size_t pos)
{
    assert(pos < bitmap->size);
    _Atomic(bitmap_container_t) *ptr = bitmap->container + WORD_INDEX(pos);
    bitmap_container_t word = atomic_load_explicit(ptr, memory_order_acquire);
    return word & BIT_MASK(pos) ? 1 : 0;
}