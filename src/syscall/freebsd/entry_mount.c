

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

#include "syscall_list.h"

struct syscall_entry entry_mount = {

    .syscall_name = "mount",
    .syscall_symbol = SYS_mount,
    .total_args = 4,
    .status = ON,
    .requires_root = NX_YES,
    .need_alarm = NX_NO,

    .arg_type_array[FIRST_ARG] = MOUNT_TYPE,
    .get_arg_array[FIRST_ARG] = &generate_mount_type,

    .arg_type_array[SECOND_ARG] = MOUNT_PATH,
    .get_arg_array[SECOND_ARG] = &generate_mountpath,

    .arg_type_array[THIRD_ARG] = MOUNT_FLAG,
    .get_arg_array[THIRD_ARG] = &generate_mount_flags,

    .arg_type_array[FOURTH_ARG] = VOID_BUF,
    .get_arg_array[FOURTH_ARG] = &generate_buf,
};