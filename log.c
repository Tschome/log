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
#include "bprint.h"
#include "common.h"
//#include "internal.h"
#include "log.h"
#include <pthread.h>

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

