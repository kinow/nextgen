

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

#include "syscall_list.h"

struct syscall_entry entry_creat = {

    .syscall_name = "creat",
    .syscall_symbol = SYS_creat,
    .total_args = 2,
    .status = ON,
    .requires_root = NX_NO,
    .need_alarm = NX_NO,

    .arg_type_array[FIRST_ARG] = FILE_PATH,
    .get_arg_array[FIRST_ARG] = &generate_path,

    .arg_type_array[SECOND_ARG] = MODE,
    .get_arg_array[SECOND_ARG] = &generate_mode

};