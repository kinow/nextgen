/**
 * Copyright (c) 2015, Harrison Bowden, Minneapolis, MN
 *
 * Permission to use, copy, modify, and/or distribute this software for any purpose
 * with or without fee is hereby granted, provided that the above copyright notice
 * and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
 * WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 **/

#ifndef IO_H
#define IO_H

#include "utils/deprecate.h"
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>
#include <dirent.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/param.h>

/* MACROS for map_file_in(). */
#define READ PROT_READ
#define WRITE PROT_WRITE
#define EXEC PROT_EXEC
#define NONE PROT_NONE

/* The enum used to tell output how to output the message. */
enum out_type { ERROR, STD };

struct output_writter
{
    void (*write)(enum out_type type, const char *format, ...);
};

extern struct output_writter *get_console_writter(void);

/* This function replaces printf and perror in the code so we can aggregate output to one point. */
DEPRECATED void output(enum out_type type, const char *format, ...);

extern void set_verbosity(int32_t val);

#endif
