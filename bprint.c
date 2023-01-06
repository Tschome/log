/*
 * Copyright (c) 2012 Nicolas George
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

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
//#include "avstring.h"
#include "bprint.h"
//#include "compat/va_copy.h"
#include "error.h"
//#include "macros.h"
//#include "mem.h"
#include "common.h"

#define bprint_room(buf) ((buf)->size - FFMIN((buf)->len, (buf)->size))
#define bprint_is_allocated(buf) ((buf)->str != (buf)->reserved_internal_buffer)

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


