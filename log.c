/*
 * log functions
 * Copyright (c) 2003 Michel Bardiaux
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * logging functions
 */

#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#if HAVE_IO_H
#include <io.h>
#endif
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <errno.h>
#include <time.h>
#include "error.h"

#include "log.h"

typedef enum {
    CLASS_CATEGORY_NA = 0,
    CLASS_CATEGORY_INPUT,
    CLASS_CATEGORY_OUTPUT,
    CLASS_CATEGORY_MUXER,
    CLASS_CATEGORY_DEMUXER,
    CLASS_CATEGORY_ENCODER,
    CLASS_CATEGORY_DECODER,
    CLASS_CATEGORY_FILTER,
    CLASS_CATEGORY_BITSTREAM_FILTER,
    CLASS_CATEGORY_SWSCALER,
    CLASS_CATEGORY_SWRESAMPLER,
    CLASS_CATEGORY_DEVICE_VIDEO_OUTPUT = 40,
    CLASS_CATEGORY_DEVICE_VIDEO_INPUT,
    CLASS_CATEGORY_DEVICE_AUDIO_OUTPUT,
    CLASS_CATEGORY_DEVICE_AUDIO_INPUT,
    CLASS_CATEGORY_DEVICE_OUTPUT,
    CLASS_CATEGORY_DEVICE_INPUT,
    CLASS_CATEGORY_NB  ///< not part of ABI/API
}AVClassCategory;

#define IS_INPUT_DEVICE(category) \
    (((category) == CLASS_CATEGORY_DEVICE_VIDEO_INPUT) || \
     ((category) == CLASS_CATEGORY_DEVICE_AUDIO_INPUT) || \
     ((category) == CLASS_CATEGORY_DEVICE_INPUT))

#define IS_OUTPUT_DEVICE(category) \
    (((category) == CLASS_CATEGORY_DEVICE_VIDEO_OUTPUT) || \
     ((category) == CLASS_CATEGORY_DEVICE_AUDIO_OUTPUT) || \
     ((category) == CLASS_CATEGORY_DEVICE_OUTPUT))

#define isatty(fd) 1


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

 
#define bprint_room(buf) ((buf)->size - FFMIN((buf)->len, (buf)->size))
#define bprint_is_allocated(buf) ((buf)->str != (buf)->reserved_internal_buffer)







void *realloc(void *ptr, size_t size)
{
	void *ret;

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


	if (size > atomic_load_explicit(&max_alloc_size, memory_order_relaxed))
		return NULL;

#if HAVE_ALIGNED_MALLOC
    ret = _aligned_realloc(ptr, size + !size, ALIGN);
#else
    ret = realloc(ptr, size + !size);
#endif
#if CONFIG_MEMORY_POISONING
    if (ret && !ptr)
        memset(ret, FF_MEMORY_POISON, size);
#endif
    return ret;
}

static void *memdup(const void *p, size_t size)
{
    void *ptr = NULL;
    if (p) {
        ptr = malloc(size);
        if (ptr)
            memcpy(ptr, p, size);
    }
    return ptr;
}

static void freep(void *arg)
{
    void *val;

    memcpy(&val, arg, sizeof(val));
    memcpy(arg, &(void *){ NULL }, sizeof(val));
    free(val);
}

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


/**
 * Define a structure with extra padding to a fixed size
 * This helps ensuring binary compatibility with future versions.
 */

#define FF_PAD_STRUCTURE(name, size, ...) \
struct ff_pad_helper_##name { __VA_ARGS__ }; \
typedef struct name { \
    __VA_ARGS__ \
    char reserved_padding[size - sizeof(struct ff_pad_helper_##name)]; \
} name;

/**
 * Buffer to print data progressively
 *
 * The string buffer grows as necessary and is always 0-terminated.
 * The content of the string is never accessed, and thus is
 * encoding-agnostic and can even hold binary data.
 *
 * Small buffers are kept in the structure itself, and thus require no
 * memory allocation at all (unless the contents of the buffer is needed
 * after the structure goes out of scope). This is almost as lightweight as
 * declaring a local "char buf[512]".
 *
 * The length of the string can go beyond the allocated size: the buffer is
 * then truncated, but the functions still keep account of the actual total
 * length.
 *
 * In other words, buf->len can be greater than buf->size and records the
 * total length of what would have been to the buffer if there had been
 * enough memory.
 *
 * Append operations do not need to be tested for failure: if a memory
 * allocation fails, data stop being appended to the buffer, but the length
 * is still updated. This situation can be tested with
 * bprint_is_complete().
 *
 * The size_max field determines several possible behaviours:
 *
 * size_max = -1 (= UINT_MAX) or any large value will let the buffer be
 * reallocated as necessary, with an amortized linear cost.
 *
 * size_max = 0 prevents writing anything to the buffer: only the total
 * length is computed. The write operations can then possibly be repeated in
 * a buffer with exactly the necessary size
 * (using size_init = size_max = len + 1).
 *
 * size_max = 1 is automatically replaced by the exact size available in the
 * structure itself, thus ensuring no dynamic memory allocation. The
 * internal buffer is large enough to hold a reasonable paragraph of text,
 * such as the current paragraph.
 */

FF_PAD_STRUCTURE(AVBPrint, 1024,
    char *str;         /**< string so far */
    unsigned len;      /**< length so far */
    unsigned size;     /**< allocated memory */
    unsigned size_max; /**< maximum allocated memory */
    char reserved_internal_buffer[1];
)

/**
 * Convenience macros for special values for bprint_init() size_max
 * parameter.
 */
#define BPRINT_SIZE_UNLIMITED  ((unsigned)-1)
#define BPRINT_SIZE_AUTOMATIC  1
#define BPRINT_SIZE_COUNT_ONLY 0

/**
 * Init a print buffer.
 *
 * @param buf        buffer to init
 * @param size_init  initial size (including the final 0)
 * @param size_max   maximum size;
 *                   0 means do not write anything, just count the length;
 *                   1 is replaced by the maximum value for automatic storage;
 *                   any large value means that the internal buffer will be
 *                   reallocated as needed up to that limit; -1 is converted to
 *                   UINT_MAX, the largest limit possible.
 *                   Check also BPRINT_SIZE_* macros.
 */
void bprint_init(AVBPrint *buf, unsigned size_init, unsigned size_max);

/**
 * Init a print buffer using a pre-existing buffer.
 *
 * The buffer will not be reallocated.
 *
 * @param buf     buffer structure to init
 * @param buffer  byte buffer to use for the string data
 * @param size    size of buffer
 */
void bprint_init_for_buffer(AVBPrint *buf, char *buffer, unsigned size);

/**
 * Append a formatted string to a print buffer.
 */
void bprintf(AVBPrint *buf, const char *fmt, ...) printf_format(2, 3);

/**
 * Append a formatted string to a print buffer.
 */
void vbprintf(AVBPrint *buf, const char *fmt, va_list vl_arg);

/**
 * Append char c n times to a print buffer.
 */
void bprint_chars(AVBPrint *buf, char c, unsigned n);

/**
 * Append data to a print buffer.
 *
 * param buf  bprint buffer to use
 * param data pointer to data
 * param size size of data
 */
void bprint_append_data(AVBPrint *buf, const char *data, unsigned size);

struct tm;
/**
 * Append a formatted date and time to a print buffer.
 *
 * param buf  bprint buffer to use
 * param fmt  date and time format string, see strftime()
 * param tm   broken-down time structure to translate
 *
 * @note due to poor design of the standard strftime function, it may
 * produce poor results if the format string expands to a very long text and
 * the bprint buffer is near the limit stated by the size_max option.
 */
void bprint_strftime(AVBPrint *buf, const char *fmt, const struct tm *tm);

/**
 * Allocate bytes in the buffer for external use.
 *
 * @param[in]  buf          buffer structure
 * @param[in]  size         required size
 * @param[out] mem          pointer to the memory area
 * @param[out] actual_size  size of the memory area after allocation;
 *                          can be larger or smaller than size
 */
void bprint_get_buffer(AVBPrint *buf, unsigned size,
                          unsigned char **mem, unsigned *actual_size);

/**
 * Reset the string to "" but keep internal allocated data.
 */
void bprint_clear(AVBPrint *buf);

/**
 * Test if the print buffer is complete (not truncated).
 *
 * It may have been truncated due to a memory allocation failure
 * or the size_max limit (compare size and size_max if necessary).
 */
static inline int bprint_is_complete(const AVBPrint *buf)
{
    return buf->len < buf->size;
}

/**
 * Finalize a print buffer.
 *
 * The print buffer can no longer be used afterwards,
 * but the len and size fields are still valid.
 *
 * @arg[out] ret_str  if not NULL, used to return a permanent copy of the
 *                    buffer contents, or NULL if memory allocation fails;
 *                    if NULL, the buffer is discarded and freed
 * @return  0 for success or error code (probably AVERROR(ENOMEM))
 */
int bprint_finalize(AVBPrint *buf, char **ret_str);
















static int bprint_alloc(AVBPrint *buf, unsigned room)
{
    char *old_str, *new_str;
    unsigned min_size, new_size;

    if (buf->size == buf->size_max)
        return AVERROR(EIO);
    if (!bprint_is_complete(buf))
        return AVERROR_INVALIDDATA; /* it is already truncated anyway */
    min_size = buf->len + 1 + FFMIN(UINT_MAX - buf->len - 1, room);
    new_size = buf->size > buf->size_max / 2 ? buf->size_max : buf->size * 2;
    if (new_size < min_size)
        new_size = FFMIN(buf->size_max, min_size);
    old_str = bprint_is_allocated(buf) ? buf->str : NULL;
    new_str = realloc(old_str, new_size);
    if (!new_str)
        return AVERROR(ENOMEM);
    if (!old_str)
        memcpy(new_str, buf->str, buf->len + 1);
    buf->str  = new_str;
    buf->size = new_size;
    return 0;
}

static void bprint_grow(AVBPrint *buf, unsigned extra_len)
{
    /* arbitrary margin to avoid small overflows */
    extra_len = FFMIN(extra_len, UINT_MAX - 5 - buf->len);
    buf->len += extra_len;
    if (buf->size)
        buf->str[FFMIN(buf->len, buf->size - 1)] = 0;
}

void bprint_init(AVBPrint *buf, unsigned size_init, unsigned size_max)
{
    unsigned size_auto = (char *)buf + sizeof(*buf) -
                         buf->reserved_internal_buffer;

    if (size_max == 1)
        size_max = size_auto;
    buf->str      = buf->reserved_internal_buffer;
    buf->len      = 0;
    buf->size     = FFMIN(size_auto, size_max);
    buf->size_max = size_max;
    *buf->str = 0;
    if (size_init > buf->size)
        bprint_alloc(buf, size_init - 1);
}

void bprint_init_for_buffer(AVBPrint *buf, char *buffer, unsigned size)
{
    buf->str      = buffer;
    buf->len      = 0;
    buf->size     = size;
    buf->size_max = size;
    *buf->str = 0;
}

void bprintf(AVBPrint *buf, const char *fmt, ...)
{
    unsigned room;
    char *dst;
    va_list vl;
    int extra_len;

    while (1) {
        room = bprint_room(buf);
        dst = room ? buf->str + buf->len : NULL;
        va_start(vl, fmt);
        extra_len = vsnprintf(dst, room, fmt, vl);
        va_end(vl);
        if (extra_len <= 0)
            return;
        if (extra_len < room)
            break;
        if (bprint_alloc(buf, extra_len))
            break;
    }
    bprint_grow(buf, extra_len);
}

void vbprintf(AVBPrint *buf, const char *fmt, va_list vl_arg)
{
    unsigned room;
    char *dst;
    int extra_len;
    va_list vl;

    while (1) {
        room = bprint_room(buf);
        dst = room ? buf->str + buf->len : NULL;
        va_copy(vl, vl_arg);
        extra_len = vsnprintf(dst, room, fmt, vl);
        va_end(vl);
        if (extra_len <= 0)
            return;
        if (extra_len < room)
            break;
        if (bprint_alloc(buf, extra_len))
            break;
    }
    bprint_grow(buf, extra_len);
}

void bprint_chars(AVBPrint *buf, char c, unsigned n)
{
    unsigned room, real_n;

    while (1) {
        room = bprint_room(buf);
        if (n < room)
            break;
        if (bprint_alloc(buf, n))
            break;
    }
    if (room) {
        real_n = FFMIN(n, room - 1);
        memset(buf->str + buf->len, c, real_n);
    }
    bprint_grow(buf, n);
}

void bprint_append_data(AVBPrint *buf, const char *data, unsigned size)
{
    unsigned room, real_n;

    while (1) {
        room = bprint_room(buf);
        if (size < room)
            break;
        if (bprint_alloc(buf, size))
            break;
    }
    if (room) {
        real_n = FFMIN(size, room - 1);
        memcpy(buf->str + buf->len, data, real_n);
    }
    bprint_grow(buf, size);
}

void bprint_strftime(AVBPrint *buf, const char *fmt, const struct tm *tm)
{
    unsigned room;
    size_t l;

    if (!*fmt)
        return;
    while (1) {
        room = bprint_room(buf);
        if (room && (l = strftime(buf->str + buf->len, room, fmt, tm)))
            break;
        /* strftime does not tell us how much room it would need: let us
           retry with twice as much until the buffer is large enough */
        room = !room ? strlen(fmt) + 1 :
               room <= INT_MAX / 2 ? room * 2 : INT_MAX;
        if (bprint_alloc(buf, room)) {
            /* impossible to grow, try to manage something useful anyway */
            room = bprint_room(buf);
            if (room < 1024) {
                /* if strftime fails because the buffer has (almost) reached
                   its maximum size, let us try in a local buffer; 1k should
                   be enough to format any real date+time string */
                char buf2[1024];
                if ((l = strftime(buf2, sizeof(buf2), fmt, tm))) {
                    bprintf(buf, "%s", buf2);
                    return;
                }
            }
            if (room) {
                /* if anything else failed and the buffer is not already
                   truncated, let us add a stock string and force truncation */
                static const char txt[] = "[truncated strftime output]";
                memset(buf->str + buf->len, '!', room);
                memcpy(buf->str + buf->len, txt, FFMIN(sizeof(txt) - 1, room));
                bprint_grow(buf, room); /* force truncation */
            }
            return;
        }
    }
    bprint_grow(buf, l);
}

void bprint_get_buffer(AVBPrint *buf, unsigned size,
                          unsigned char **mem, unsigned *actual_size)
{
    if (size > bprint_room(buf))
        bprint_alloc(buf, size);
    *actual_size = bprint_room(buf);
    *mem = *actual_size ? buf->str + buf->len : NULL;
}

void bprint_clear(AVBPrint *buf)
{
    if (buf->len) {
        *buf->str = 0;
        buf->len  = 0;
    }
}

int bprint_finalize(AVBPrint *buf, char **ret_str)
{
    unsigned real_size = FFMIN(buf->len + 1, buf->size);
    char *str;
    int ret = 0;

    if (ret_str) {
        if (bprint_is_allocated(buf)) {
            str = realloc(buf->str, real_size);
            if (!str)
                str = buf->str;
            buf->str = NULL;
        } else {
            str = memdup(buf->str, real_size);
            if (!str)
                ret = AVERROR(ENOMEM);
        }
        *ret_str = str;
    } else {
        if (bprint_is_allocated(buf))
            freep(&buf->str);
    }
    buf->size = real_size;
    return ret;
}

#define WHITESPACES " \n\t\r"



/******************************************************************************************************/

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

#define LINE_SZ 1024

#if HAVE_VALGRIND_VALGRIND_H
#include <valgrind/valgrind.h>
/* this is the log level at which valgrind will output a full backtrace */
#define BACKTRACE_LOGLEVEL LOG_ERROR
#endif

static int log_level = LOG_INFO;
static int flags;

#define NB_LEVELS 8
#if defined(_WIN32) && HAVE_SETCONSOLETEXTATTRIBUTE && HAVE_GETSTDHANDLE
#include <windows.h>
static const uint8_t color[16 + CLASS_CATEGORY_NB] = {
    [LOG_PANIC  /8] = 12,
    [LOG_FATAL  /8] = 12,
    [LOG_ERROR  /8] = 12,
    [LOG_WARNING/8] = 14,
    [LOG_INFO   /8] =  7,
    [LOG_VERBOSE/8] = 10,
    [LOG_DEBUG  /8] = 10,
    [LOG_TRACE  /8] = 8,
    [16+CLASS_CATEGORY_NA              ] =  7,
    [16+CLASS_CATEGORY_INPUT           ] = 13,
    [16+CLASS_CATEGORY_OUTPUT          ] =  5,
    [16+CLASS_CATEGORY_MUXER           ] = 13,
    [16+CLASS_CATEGORY_DEMUXER         ] =  5,
    [16+CLASS_CATEGORY_ENCODER         ] = 11,
    [16+CLASS_CATEGORY_DECODER         ] =  3,
    [16+CLASS_CATEGORY_FILTER          ] = 10,
    [16+CLASS_CATEGORY_BITSTREAM_FILTER] =  9,
    [16+CLASS_CATEGORY_SWSCALER        ] =  7,
    [16+CLASS_CATEGORY_SWRESAMPLER     ] =  7,
    [16+CLASS_CATEGORY_DEVICE_VIDEO_OUTPUT ] = 13,
    [16+CLASS_CATEGORY_DEVICE_VIDEO_INPUT  ] = 5,
    [16+CLASS_CATEGORY_DEVICE_AUDIO_OUTPUT ] = 13,
    [16+CLASS_CATEGORY_DEVICE_AUDIO_INPUT  ] = 5,
    [16+CLASS_CATEGORY_DEVICE_OUTPUT       ] = 13,
    [16+CLASS_CATEGORY_DEVICE_INPUT        ] = 5,
};

static int16_t background, attr_orig;
static HANDLE con;
#else

static const uint32_t color[16 + CLASS_CATEGORY_NB] = {
    [LOG_PANIC  /8] =  52 << 16 | 196 << 8 | 0x41,
    [LOG_FATAL  /8] = 208 <<  8 | 0x41,
    [LOG_ERROR  /8] = 196 <<  8 | 0x11,
    [LOG_WARNING/8] = 226 <<  8 | 0x03,
    [LOG_INFO   /8] = 253 <<  8 | 0x09,
    [LOG_VERBOSE/8] =  40 <<  8 | 0x02,
    [LOG_DEBUG  /8] =  34 <<  8 | 0x02,
    [LOG_TRACE  /8] =  34 <<  8 | 0x07,
    [16+CLASS_CATEGORY_NA              ] = 250 << 8 | 0x09,
    [16+CLASS_CATEGORY_INPUT           ] = 219 << 8 | 0x15,
    [16+CLASS_CATEGORY_OUTPUT          ] = 201 << 8 | 0x05,
    [16+CLASS_CATEGORY_MUXER           ] = 213 << 8 | 0x15,
    [16+CLASS_CATEGORY_DEMUXER         ] = 207 << 8 | 0x05,
    [16+CLASS_CATEGORY_ENCODER         ] =  51 << 8 | 0x16,
    [16+CLASS_CATEGORY_DECODER         ] =  39 << 8 | 0x06,
    [16+CLASS_CATEGORY_FILTER          ] = 155 << 8 | 0x12,
    [16+CLASS_CATEGORY_BITSTREAM_FILTER] = 192 << 8 | 0x14,
    [16+CLASS_CATEGORY_SWSCALER        ] = 153 << 8 | 0x14,
    [16+CLASS_CATEGORY_SWRESAMPLER     ] = 147 << 8 | 0x14,
    [16+CLASS_CATEGORY_DEVICE_VIDEO_OUTPUT ] = 213 << 8 | 0x15,
    [16+CLASS_CATEGORY_DEVICE_VIDEO_INPUT  ] = 207 << 8 | 0x05,
    [16+CLASS_CATEGORY_DEVICE_AUDIO_OUTPUT ] = 213 << 8 | 0x15,
    [16+CLASS_CATEGORY_DEVICE_AUDIO_INPUT  ] = 207 << 8 | 0x05,
    [16+CLASS_CATEGORY_DEVICE_OUTPUT       ] = 213 << 8 | 0x15,
    [16+CLASS_CATEGORY_DEVICE_INPUT        ] = 207 << 8 | 0x05,
};

#endif
static int use_color = -1;

#if defined(_WIN32) && HAVE_SETCONSOLETEXTATTRIBUTE && HAVE_GETSTDHANDLE
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


static void win_console_puts(const char *str)
{
    const uint8_t *q = str;
    uint16_t line[LINE_SZ];

    while (*q) {
        uint16_t *buf = line;
        DWORD nb_chars = 0;
        DWORD written;

        while (*q && nb_chars < LINE_SZ - 1) {
            uint32_t ch;
            uint16_t tmp;

            GET_UTF8(ch, *q ? *q++ : 0, ch = 0xfffd; goto continue_on_invalid;)
continue_on_invalid:
            PUT_UTF16(ch, tmp, *buf++ = tmp; nb_chars++;)
        }

        WriteConsoleW(con, line, nb_chars, &written, NULL);
    }
}
#endif

static void check_color_terminal(void)
{
    char *term = getenv("TERM");

#if defined(_WIN32) && HAVE_SETCONSOLETEXTATTRIBUTE && HAVE_GETSTDHANDLE
    CONSOLE_SCREEN_BUFFER_INFO con_info;
    DWORD dummy;
    con = GetStdHandle(STD_ERROR_HANDLE);
    if (con != INVALID_HANDLE_VALUE && !GetConsoleMode(con, &dummy))
        con = INVALID_HANDLE_VALUE;
    if (con != INVALID_HANDLE_VALUE) {
        GetConsoleScreenBufferInfo(con, &con_info);
        attr_orig  = con_info.wAttributes;
        background = attr_orig & 0xF0;
    }
#endif

    if (getenv("LOG_FORCE_NOCOLOR")) {
        use_color = 0;
    } else if (getenv("LOG_FORCE_COLOR")) {
        use_color = 1;
    } else {
#if defined(_WIN32) && HAVE_SETCONSOLETEXTATTRIBUTE && HAVE_GETSTDHANDLE
        use_color = (con != INVALID_HANDLE_VALUE);
#elif HAVE_ISATTY
        use_color = (term && isatty(2));
#else
        use_color = 0;
#endif
    }

    if (getenv("LOG_FORCE_256COLOR") || term && strstr(term, "256color"))
        use_color *= 256;
}

static void ansi_fputs(int level, int tint, const char *str, int local_use_color)
{
	if (local_use_color == 1) {
		fprintf(stderr,
				"\033[%"PRIu32";3%"PRIu32"m%s\033[0m",
				(color[level] >> 4) & 15,
				color[level] & 15,
				str);
	} else if (tint && use_color == 256) {
		fprintf(stderr,
				"\033[48;5;%"PRIu32"m\033[38;5;%dm%s\033[0m",
				(color[level] >> 16) & 0xff,
				tint,
				str);
	} else if (local_use_color == 256) {
		fprintf(stderr,
				"\033[48;5;%"PRIu32"m\033[38;5;%"PRIu32"m%s\033[0m",
				(color[level] >> 16) & 0xff,
				(color[level] >> 8) & 0xff,
				str);
	} else
		fputs(str, stderr);
}

static void colored_fputs(int level, int tint, const char *str)
{
    int local_use_color;
    if (!*str)
        return;

    if (use_color < 0)
        check_color_terminal();

    if (level == LOG_INFO/8) local_use_color = 0;
    else                        local_use_color = use_color;

#if 1
	//test
	use_color = 256;
	local_use_color = 1;
#endif

#if defined(_WIN32) && HAVE_SETCONSOLETEXTATTRIBUTE && HAVE_GETSTDHANDLE
    if (con != INVALID_HANDLE_VALUE) {
        if (local_use_color)
            SetConsoleTextAttribute(con, background | color[level]);
        win_console_puts(str);
        if (local_use_color)
            SetConsoleTextAttribute(con, attr_orig);
    } else {
        ansi_fputs(level, tint, str, local_use_color);
    }
#else
    ansi_fputs(level, tint, str, local_use_color);
#endif

}

static void sanitize(uint8_t *line){
    while(*line){
        if(*line < 0x08 || (*line > 0x0D && *line < 0x20))
            *line='?';
        line++;
    }
}

static const char *get_level_str(int level)
{
    switch (level) {
    case LOG_QUIET:
        return "quiet";
    case LOG_DEBUG:
        return "debug";
    case LOG_TRACE:
        return "trace";
    case LOG_VERBOSE:
        return "verbose";
    case LOG_INFO:
        return "info";
    case LOG_WARNING:
        return "warning";
    case LOG_ERROR:
        return "error";
    case LOG_FATAL:
        return "fatal";
    case LOG_PANIC:
        return "panic";
    default:
        return "";
    }
}

static void format_line(void *name, int level, const char *fmt, va_list vl,
                        AVBPrint part[4], int *print_prefix, int type[2])
{
    bprint_init(part+0, 0, BPRINT_SIZE_AUTOMATIC);
    bprint_init(part+1, 0, BPRINT_SIZE_AUTOMATIC);
    bprint_init(part+2, 0, BPRINT_SIZE_AUTOMATIC);
    bprint_init(part+3, 0, 65536);

    if(type) type[0] = type[1] = CLASS_CATEGORY_NA + 16;

	if (*print_prefix && name) {
		bprintf(part+1, "[%s @ %s] ", (char *)name, "reserve");
	}
    if (*print_prefix && (level > LOG_QUIET) && (flags & LOG_PRINT_LEVEL))
        bprintf(part+2, "[%s] ", get_level_str(level));

    vbprintf(part+3, fmt, vl);

    if(*part[0].str || *part[1].str || *part[2].str || *part[3].str) {
        char lastc = part[3].len && part[3].len <= part[3].size ? part[3].str[part[3].len - 1] : 0;
        *print_prefix = lastc == '\n' || lastc == '\r';
    }
}

void log_format_line(void *name, int level, const char *fmt, va_list vl,
                        char *line, int line_size, int *print_prefix)
{
    log_format_line2(name, level, fmt, vl, line, line_size, print_prefix);
}

int log_format_line2(void *name, int level, const char *fmt, va_list vl,
                        char *line, int line_size, int *print_prefix)
{
    AVBPrint part[4];
    int ret;

    format_line(name, level, fmt, vl, part, print_prefix, NULL);
    ret = snprintf(line, line_size, "%s%s%s%s", part[0].str, part[1].str, part[2].str, part[3].str);
    bprint_finalize(part+3, NULL);
    return ret;
}

void log_default_callback(void *name, int level, const char* fmt, va_list vl)
{
    static int print_prefix = 1;
    static int count;
    static char prev[LINE_SZ];
    AVBPrint part[4];
    char line[LINE_SZ];
    static int is_atty;
    int type[2];
    unsigned tint = 0;

    if (level >= 0) {
        tint = level & 0xff00;
        level &= 0xff;
    }

    if (level > log_level)
        return;

	pthread_mutex_lock(&mutex);
    format_line(name, level, fmt, vl, part, &print_prefix, type);
    snprintf(line, sizeof(line), "%s%s%s%s", part[0].str, part[1].str, part[2].str, part[3].str);

    if (!is_atty)
        is_atty = isatty(2) ? 1 : -1;

    if (print_prefix && (flags & LOG_SKIP_REPEATED) && !strcmp(line, prev) &&
        *line && line[strlen(line) - 1] != '\r'){
        count++;
        if (is_atty == 1)
            fprintf(stderr, "    Last message repeated %d times\r", count);
        goto end;
    }
    if (count > 0) {
        fprintf(stderr, "    Last message repeated %d times\n", count);
        count = 0;
    }
    strcpy(prev, line);
    sanitize(part[0].str);
    colored_fputs(type[0], 0, part[0].str);
    sanitize(part[1].str);
    colored_fputs(type[1], 0, part[1].str);
    sanitize(part[2].str);
    colored_fputs(clip(level >> 3, 0, NB_LEVELS - 1), tint >> 8, part[2].str);
    sanitize(part[3].str);
    colored_fputs(clip(level >> 3, 0, NB_LEVELS - 1), tint >> 8, part[3].str);

#if CONFIG_VALGRIND_BACKTRACE
    if (level <= BACKTRACE_LOGLEVEL)
        VALGRIND_PRINTF_BACKTRACE("%s", "");
#endif
end:
    bprint_finalize(part+3, NULL);

	pthread_mutex_unlock(&mutex);
}

static void (*log_callback)(void*, int, const char*, va_list) = log_default_callback;

void log(void *name, int level, const char *fmt, ...)
{
    va_list vl;
    va_start(vl, fmt);
    vlog(name, level, fmt, vl);
    va_end(vl);
}

void log_once(void *name, int initial_level, int subsequent_level, int *state, const char *fmt, ...)
{
    va_list vl;
    va_start(vl, fmt);
    vlog(name, *state ? subsequent_level : initial_level, fmt, vl);
    va_end(vl);
    *state = 1;
}

void vlog(void *name, int level, const char *fmt, va_list vl)
{
#if 0
    void (*log_callback)(void*, int, const char*, va_list) = log_callback;
    if (log_callback){
        log_callback(name, level, fmt, vl);
    }
#else
    log_default_callback(name, level, fmt, vl);
#endif
}

int log_get_level(void)
{
    return log_level;
}

void log_set_level(int level)
{
    log_level = level;
}

void log_set_flags(int arg)
{
    flags = arg;
}

int log_get_flags(void)
{
    return flags;
}

void log_set_callback(void (*callback)(void*, int, const char*, va_list))
{
    log_callback = callback;
}

