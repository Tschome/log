/*
 * copyright (c) 2006 Michael Niedermayer <michaelni@gmx.at>
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

#ifndef AVUTIL_LOG_H
#define AVUTIL_LOG_H

#include <stdarg.h>
#include "common.h"

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

struct AVOptionRanges;

/**
 * @addtogroup lavu_log
 *
 * @{
 *
 * @defgroup lavu_log_constants Logging Constants
 *
 * @{
 */

/**
 * Print no output.
 */
#define LOG_QUIET    -8

/**
 * Something went really wrong and we will crash now.
 */
#define LOG_PANIC     0

/**
 * Something went wrong and recovery is not possible.
 * For example, no header was found for a format which depends
 * on headers or an illegal combination of parameters is used.
 */
#define LOG_FATAL     8

/**
 * Something went wrong and cannot losslessly be recovered.
 * However, not all future data is affected.
 */
#define LOG_ERROR    16

/**
 * Something somehow does not look correct. This may or may not
 * lead to problems. An example would be the use of '-vstrict -2'.
 */
#define LOG_WARNING  24

/**
 * Standard information.
 */
#define LOG_INFO     32

/**
 * Detailed information.
 */
#define LOG_VERBOSE  40

/**
 * Stuff which is only useful for libav* developers.
 */
#define LOG_DEBUG    48

/**
 * Extremely verbose debugging, useful for libav* development.
 */
#define LOG_TRACE    56

#define LOG_MAX_OFFSET (LOG_TRACE - LOG_QUIET)

/**
 * @}
 */

/**
 * Sets additional colors for extended debugging sessions.
 * @code
   log(ctx, LOG_DEBUG|AV_LOG_C(134), "Message in purple\n");
   @endcode
 * Requires 256color terminal support. Uses outside debugging is not
 * recommended.
 */
#define LOG_C(x) ((x) << 8)

/**
 * Send the specified message to the log if the level is less than or equal
 * to the current log_level. By default, all logging messages are sent to
 * stderr. This behavior can be altered by setting a different logging callback
 * function.
 * @see log_set_callback
 *
 * @param avcl A pointer to an arbitrary struct of which the first field is a
 *        pointer to an AVClass struct or NULL if general log.
 * @param level The importance level of the message expressed using a @ref
 *        lavu_log_constants "Logging Constant".
 * @param fmt The format string (printf-compatible) that specifies how
 *        subsequent arguments are converted to output.
 */
void log(void *name, int level, const char *fmt, ...) printf_format(3, 4);

/**
 * Send the specified message to the log once with the initial_level and then with
 * the subsequent_level. By default, all logging messages are sent to
 * stderr. This behavior can be altered by setting a different logging callback
 * function.
 * @see log
 *
 * @param avcl A pointer to an arbitrary struct of which the first field is a
 *        pointer to an AVClass struct or NULL if general log.
 * @param initial_level importance level of the message expressed using a @ref
 *        lavu_log_constants "Logging Constant" for the first occurance.
 * @param subsequent_level importance level of the message expressed using a @ref
 *        lavu_log_constants "Logging Constant" after the first occurance.
 * @param fmt The format string (printf-compatible) that specifies how
 *        subsequent arguments are converted to output.
 * @param state a variable to keep trak of if a message has already been printed
 *        this must be initialized to 0 before the first use. The same state
 *        must not be accessed by 2 Threads simultaneously.
 */
void log_once(void *name, int initial_level, int subsequent_level, int *state, const char *fmt, ...) printf_format(5, 6);


/**
 * Send the specified message to the log if the level is less than or equal
 * to the current log_level. By default, all logging messages are sent to
 * stderr. This behavior can be altered by setting a different logging callback
 * function.
 * @see log_set_callback
 *
 * @param avcl A pointer to an arbitrary struct of which the first field is a
 *        pointer to an AVClass struct.
 * @param level The importance level of the message expressed using a @ref
 *        lavu_log_constants "Logging Constant".
 * @param fmt The format string (printf-compatible) that specifies how
 *        subsequent arguments are converted to output.
 * @param vl The arguments referenced by the format string.
 */
void vlog(void *name, int level, const char *fmt, va_list vl);

/**
 * Get the current log level
 *
 * @see lavu_log_constants
 *
 * @return Current log level
 */
int log_get_level(void);

/**
 * Set the log level
 *
 * @see lavu_log_constants
 *
 * @param level Logging level
 */
void log_set_level(int level);

/**
 * Set the logging callback
 *
 * @note The callback must be thread safe, even if the application does not use
 *       threads itself as some codecs are multithreaded.
 *
 * @see log_default_callback
 *
 * @param callback A logging function with a compatible signature.
 */
void log_set_callback(void (*callback)(void *, int, const char*, va_list));

/**
 * Default logging callback
 *
 * It prints the message to stderr, optionally colorizing it.
 *
 * @param level The importance level of the message expressed using a @ref
 *        lavu_log_constants "Logging Constant".
 * @param fmt The format string (printf-compatible) that specifies how
 *        subsequent arguments are converted to output.
 * @param vl The arguments referenced by the format string.
 */
void log_default_callback(void *name, int level, const char *fmt,
                             va_list vl);
/**
 * Format a line of log the same way as the default callback.
 * @param line          buffer to receive the formatted line
 * @param line_size     size of the buffer
 * @param print_prefix  used to store whether the prefix must be printed;
 *                      must point to a persistent integer initially set to 1
 */
void log_format_line(void *name, int level, const char *fmt, va_list vl,
                        char *line, int line_size, int *print_prefix);

/**
 * Format a line of log the same way as the default callback.
 * @param line          buffer to receive the formatted line;
 *                      may be NULL if line_size is 0
 * @param line_size     size of the buffer; at most line_size-1 characters will
 *                      be written to the buffer, plus one null terminator
 * @param print_prefix  used to store whether the prefix must be printed;
 *                      must point to a persistent integer initially set to 1
 * @return Returns a negative value if an error occurred, otherwise returns
 *         the number of characters that would have been written for a
 *         sufficiently large buffer, not including the terminating null
 *         character. If the return value is not less than line_size, it means
 *         that the log message was truncated to fit the buffer.
 */
int log_format_line2(void *name, int level, const char *fmt, va_list vl,
                        char *line, int line_size, int *print_prefix);

/**
 * Skip repeated messages, this requires the user app to use log() instead of
 * (f)printf as the 2 would otherwise interfere and lead to
 * "Last message repeated x times" messages below (f)printf messages with some
 * bad luck.
 * Also to receive the last, "last repeated" line if any, the user app must
 * call log(NULL, LOG_QUIET, "%s", ""); at the end
 */
#define LOG_SKIP_REPEATED 1

/**
 * Include the log severity in messages originating from codecs.
 *
 * Results in messages such as:
 * [rawvideo @ 0xDEADBEEF] [error] encode did not produce valid pts
 */
#define LOG_PRINT_LEVEL 2

void log_set_flags(int arg);
int log_get_flags(void);

/**
 * @}
 */

#endif /* AVUTIL_LOG_H */
