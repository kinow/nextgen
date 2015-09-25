

/**
 * Copyright (c) 2015, Harrison Bowden, Secure Labs, Minneapolis, MN
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

#include "io.h"
#include "utils.h"

#include <errno.h>
#include <sys/mman.h>

void output(enum out_type type, const char *format, ...)
{
	va_list args;

	va_start(args, format);

	switch((int)type)
	{
		case ERROR:
		    vfprintf(stderr, format, args);
		    break;

		case STD:
		    vfprintf(stdout, format, args);
		    break;

        /* We default to stdout when ERROR or STD is not set so that
        we don't have any errors. */
		default:
		    vfprintf(stdout, format, args);
		    break;
	}

	va_end(args);

    (void)fflush(stdout);

	return;
}

int map_file_in(int fd, char **buf, off_t *size)
{
	int rtrn;

    rtrn = get_file_size(fd, size);
    if(rtrn < 0)
    {
    	output(ERROR, "Can't get file size\n");
    	return -1;
    }

    *buf = mmap(0, (unsigned long) *size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    if(*buf == MAP_FAILED)
    {
    	output(ERROR, "mmap: %s\n", strerror(errno));
    	return -1;
    }

	return 0;
}

int map_file_out(char *path, char *buf, off_t size)
{
    int fd;

    fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0777);
    if(fd < 0)
    {
    	output(ERROR, "open: %s\n", strerror(errno));
    	return -1;
    }

    if(lseek(fd, size - 1, SEEK_SET) == -1)
    {
    	output(ERROR, "lseek: %s\n", strerror(errno));
    	return -1;
    }

    ssize_t ret = write(fd, "", 1);
    if(ret != 1)
    {
    	output(ERROR, "write: %s\n", strerror(errno));
    	return -1;
    }

    char *dst = mmap(0, (unsigned long) size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    if(dst == MAP_FAILED)
    {
    	output(ERROR, "mmap: %s\n", strerror(errno));
    	return -1;
    }

    memcpy(dst, buf, (unsigned long)size);

    munmap(dst, (unsigned long)size);

    close(fd);

	return 0;
}

int copy_file_to(char *src, char *dst)
{
    int rtrn;
    int file auto_close = 0;
    off_t file_size;
    char *file_buffer;

    file = open(src, O_RDONLY);
    if(file < 0)
    {
        output(ERROR, "Can't open file: %s\n", strerror(errno));
        return -1;
    }

    rtrn = map_file_in(file, &file_buffer, &file_size);
    if(rtrn < 0)
    {
        output(ERROR, "Can't read file to memory\n");
        return -1;
    }

    rtrn = map_file_out(dst, file_buffer, file_size);
    if(rtrn < 0)
    {
        output(ERROR, "Can't write file to disk\n");
        return -1;
    }

    rtrn = fsync(file);
    if(rtrn < 0)
    {
        output(ERROR, "Can't sync file on disk\n");
        return -1;
    }

    munmap(file_buffer, file_size);

    return 0;
}