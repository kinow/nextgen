/**
 * Copyright (c) 2016, Harrison Bowden, Minneapolis, MN
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

#include "runtime/nextgen.h"
#include "runtime/fuzzer.h"
#include "memory/memory.h"
#include "objc/objc-utils.h"
#include "io/io.h"

int main(int argc, const char * argv[])
{
    struct fuzzer_config *config = NULL;
    struct output_writter *output = NULL;
    struct memory_allocator *allocator = NULL;

    /* Use the console/terminal for our output device. */
    output = get_console_writter();
    if(output == NULL)
    {
        /* Default to the console if we fail to create a output writter. */
        printf("Failed to get console writter\n");
        return (-1);
    }

    /* Get an allocator object so we can allocate other objects in our program. */
    allocator = get_default_allocator();
    if(allocator == NULL)
    {
        output->write(ERROR, "Failed to get memory allocator\n");
        return (-1);
    }

    /* Parse the command line and return the fuzzer configuration object.
      The config object will tell us how to setup the nextgen fuzzer. */
    config = parse_cmd_line(argc, argv, output, allocator);
    if(config == NULL)
        return (-1);

    return (0);
}
