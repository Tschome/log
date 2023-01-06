#ifndef __LOG_COMMON__
#define __LOG_COMMON__

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "error.h"

/** 
 * Comparator.
 * For two numerical expressions x and y, gives 1 if x > y, -1 if x < y, and 0
 * if x == y. This is useful for instance in a qsort comparator callback.
 * Furthermore, compilers are able to optimize this to branchless code, and
 * there is no risk of overflow with signed types.
 * As with many macros, this evaluates its argument multiple times, it thus
 * must not have a side-effect.
 */

#define FFMAX(a,b) ((a) > (b) ? (a) : (b))
#define FFMIN(a,b) ((a) > (b) ? (b) : (a))
    
/* error handling */
#if 1
#define AVERROR(e) (-(e))   ///< Returns a negative error code from a POSIX error code, to return from library functions.
#define AVUNERROR(e) (-(e)) ///< Returns a POSIX error code from a library function error return value.
#else
/* Some platforms have E* and errno already negated. */
#define AVERROR(e) (e)
#define AVUNERROR(e) (e)
#endif  

#define MKTAG(a,b,c,d)   ((a) | ((b) << 8) | ((c) << 16) | ((unsigned)(d) << 24))
#define FFERRTAG(a, b, c, d) (-(int)MKTAG(a, b, c, d))
#define AVERROR_INVALIDDATA        FFERRTAG( 'I','N','D','A') ///< Invalid data found when processing input

#define ATOMIC_VAR_INIT(value) (value)
/* NOTE: if you want to override these functions with your own
 * implementations (not recommended) you have to link libav* as
 * dynamic libraries and remove -Wl,-Bsymbolic from the linker flags.
 * Note that this will cost performance. */

static intptr_t max_alloc_size = ATOMIC_VAR_INIT(2147483647);


#define atomic_load(object) \
    (*(object))

#define atomic_load_explicit(object, order) \
    atomic_load(object)

#define isatty(fd) 1

#define printf_format(fmtpos, attrpos) __attribute__((__format__(__printf__, fmtpos, attrpos)))
//#define printf_format(fmtpos, attrpos)



/**
 * Convert a UTF-8 character (up to 4 bytes) to its 32-bit UCS-4 encoded form.
 *  
 * @param val      Output value, must be an lvalue of type uint32_t.
 * @param GET_BYTE Expression reading one byte from the input.
 *                 Evaluated up to 7 times (4 for the currently
 *                 assigned Unicode range).  With a memory buffer
 *                 input, this could be *ptr++, or if you want to make sure
 *                 that *ptr stops at the end of a NULL terminated string then
 *                 *ptr ? *ptr++ : 0
 * @param ERROR    Expression to be evaluated on invalid input,
 *                 typically a goto statement.
 *
 * @warning ERROR should not contain a loop control statement which
 * could interact with the internal while loop, and should force an
 * exit from the macro code (e.g. through a goto or a return) in order
 * to prevent undefined results.
 */
#define GET_UTF8(val, GET_BYTE, ERROR)\
    val= (GET_BYTE);\
    {\
		uint32_t top = (val & 128) >> 1;\
        if ((val & 0xc0) == 0x80 || val >= 0xFE)\
            {ERROR}\
        while (val & top) {\
            unsigned int tmp = (GET_BYTE) - 128;\
            if(tmp>>6)\
                {ERROR}\
            val= (val<<6) + tmp;\
            top <<= 5;\
        }\
        val &= (top << 1) - 1;\
    }

/**
 * @def PUT_UTF16(val, tmp, PUT_16BIT)
 * Convert a 32-bit Unicode character to its UTF-16 encoded form (2 or 4 bytes).
 * @param val is an input-only argument and should be of type uint32_t. It holds
 * a UCS-4 encoded Unicode character that is to be converted to UTF-16. If
 * val is given as a function it is executed only once.
 * @param tmp is a temporary variable and should be of type uint16_t. It
 * represents an intermediate value during conversion that is to be
 * output by PUT_16BIT.
 * @param PUT_16BIT writes the converted UTF-16 data to any proper destination
 * in desired endianness. It could be a function or a statement, and uses tmp
 * as the input byte.  For example, PUT_BYTE could be "*output++ = tmp;"
 * PUT_BYTE will be executed 1 or 2 times depending on input character.
 */
#define PUT_UTF16(val, tmp, PUT_16BIT)\
    {\
        uint32_t in = val;\
        if (in < 0x10000) {\
            tmp = in;\
            PUT_16BIT\
        } else {\
            tmp = 0xD800 | ((in - 0x10000) >> 10);\
            PUT_16BIT\
            tmp = 0xDC00 | ((in - 0x10000) & 0x3FF);\
            PUT_16BIT\
        }\
    }\

/**
 * Allocate, reallocate, or free a block of memory.
 *  
 * If `ptr` is `NULL` and `size` > 0, allocate a new block. Otherwise, expand or
 * shrink that block of memory according to `size`.
 *
 * @param ptr  Pointer to a memory block already allocated with
 *             realloc() or `NULL`
 * @param size Size in bytes of the memory block to be allocated or
 *             reallocated
 *
 * @return Pointer to a newly-reallocated block or `NULL` if the block
 *         cannot be reallocated
 *
 * @warning Unlike malloc(), the returned pointer is not guaranteed to be
 *          correctly aligned. The returned pointer must be freed after even
 *          if size is zero.
 * @see fast_realloc()
 * @see reallocp()
 */ 
void *realloc(void *ptr, size_t size);

/**
 * Duplicate a buffer with av_malloc().
 *
 * @param p    Buffer to be duplicated
 * @param size Size in bytes of the buffer copied
 * @return Pointer to a newly allocated buffer containing a
 *         copy of `p` or `NULL` if the buffer cannot be allocated
 */
void *memdup(const void *p, size_t size);

/**
 * Free a memory block which has been allocated with a function of av_malloc()
 * or av_realloc() family, and set the pointer pointing to it to `NULL`.
 *
 * @code{.c}
 * uint8_t *buf = av_malloc(16);
 * av_free(buf);
 * // buf now contains a dangling pointer to freed memory, and accidental
 * // dereference of buf will result in a use-after-free, which may be a
 * // security risk.
 *
 * uint8_t *buf = av_malloc(16);
 * av_freep(&buf);
 * // buf is now NULL, and accidental dereference will only result in a
 * // NULL-pointer dereference.
 * @endcode
 *
 * @param ptr Pointer to the pointer to the memory block which should be freed
 * @note `*ptr = NULL` is safe and leads to no action.
 * @see av_free()
 */
void freep(void *ptr);


/**
 * Clip a signed integer value into the amin-amax range.
 * @param a value to clip
 * @param amin minimum value of the clip range
 * @param amax maximum value of the clip range
 * @return clipped value
 */
static inline const int clip(int a, int amin, int amax)
{
    if (amin > amax) abort();

    if      (a < amin) return amin;
    else if (a > amax) return amax;
    else               return a;
}

#endif //__LOG_COMMON__
