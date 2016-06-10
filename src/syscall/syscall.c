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

#include "syscall.h"
#include "arg_types.h"
#include "crypto/crypto.h"
#include "entry.h"
#include "io/io.h"
#include "genetic/job.h"
#include "log/log.h"
#include "memory/memory.h"
#include "mutate/mutate.h"
#include "runtime/platform.h"
#include "probe/probe.h"
#include "resource/resource.h"
#include "signals.h"
#include "set_test.h"
#include "syscall_table.h"
#include "utils/utils.h"

#include <assert.h>
#include <errno.h>
#include <string.h>

/* The total number of children process to run. */
uint32_t number_of_children;

/* An array of child context structures. These structures track variables
   local to the child process. */
static struct child_ctx **children;

/* The number of children processes currently running. */
static uint32_t running_children;

/* Counter for number of syscall test that have been completed. */
static uint32_t *test_counter;

/* The syscall table. */
static struct syscall_table_shadow *sys_table;

/* A copy of the stop pointer. If the atomic int is 
   set to NX_YES than we stop fuzzing and exit. */
static int32_t *stop;

/* This variable tells us wether were in smart mode or dumb mode. */
static int32_t mode;

int32_t cleanup_syscall_table(void) { return (0); }

int32_t get_total_syscalls(uint32_t *total)
{
    (*total) = sys_table->number_of_syscalls;

    return (0);
}

struct syscall_entry_shadow *get_entry(uint32_t syscall_number)
{
    /* If the syscall number passed is greater than the total number
    of syscalls return NULL. */
    if(syscall_number > sys_table->number_of_syscalls)
        return (NULL);

    struct syscall_entry_shadow *entry = NULL;

    /* Allocate a syscall entry. */
    entry = mem_alloc(sizeof(struct syscall_entry_shadow));
    if(entry == NULL)
    {
        output(ERROR, "Can't allocate syscall entry\n");
        return (NULL);
    }

    /* Set the entry to the one in the syscall table. */
    entry = sys_table->sys_entry[syscall_number];

    return (entry);
}

/* Don't use inside of the syscall module. This function is for 
Other modules like the reaper or genetic module to get child processes. */
struct child_ctx *get_child_from_index(uint32_t i)
{
    if(i > number_of_children)
        return (NULL);

    struct child_ctx *child = NULL;

    child = mem_alloc(sizeof(struct child_ctx));
    if(child == NULL)
    {
        output(ERROR, "Can't allocate child context\n");
        return (NULL);
    }

    child = children[i];

    return (child);
}

static int32_t get_child_index_number(uint32_t *index_num)
{
    uint32_t i = 0;
    pid_t pid = getpid();

    /* Walk child array until our PID matches the one in the context struct. */
    for(i = 0; i < number_of_children; i++)
    {
        if(ck_pr_load_int(&children[i]->pid) == pid)
        {
            /* The PIDS match so set the index number and exit the function. */
            (*index_num) = i;
            return (0);
        }
    }
    /* Should not get here, but if we do return an error. */
    return (-1);
}

NX_NO_RETURN static void exit_child(void)
{
    int32_t rtrn = 0;
    struct child_ctx *ctx = NULL;

    /* Get our childs context object. */
    ctx = get_child_ctx();
    if(ctx == NULL)
    {
        output(ERROR, "Can't get child context\n");
        _exit(-1);
    }

    /* Clean up kernel probes. */
    rtrn = cleanup_kernel_probes(ctx->probe_handle);
    if(rtrn < 0)
    {
        output(ERROR, "Can't clean up kernel probes");
        _exit(-1);
    }

    /* Set the PID as empty. */
    cas_loop_int32(&ctx->pid, EMPTY);

    /* Decrement the running child counter. */
    ck_pr_dec_uint(&running_children);

    /* Exit and cleanup child. */
    _exit(0);
}

static int32_t free_old_arguments(struct child_ctx *ctx)
{
    uint32_t i;
    int32_t rtrn = 0;
    uint32_t total_args = ctx->total_args;
    struct syscall_entry_shadow *entry = NULL;

    /* Grab the syscall entry. */
    entry = get_entry(ctx->syscall_number);
    if(entry == NULL)
    {
        output(ERROR, "Can't get entry\n");
        return (-1);
    }

    for(i = 0; i < total_args; i++)
    {
        /* Handle args that require special cleanup procedures. */
        switch((int32_t)entry->arg_context_array[i]->type)
        {
            /* Below is the resource types ie they are from the resource module. 
            They must be freed using special functions and the free must be done on 
            the arg_copy_index so the free_* functions don't use the mutated value in arg_value_index. */
            case FILE_DESC:
                rtrn = free_desc((int32_t *)ctx->arg_copy_array[i]);
                if(rtrn < 0)
                    output(ERROR, "Can't free descriptor\n");
                /* Don't return on errors, just keep looping. */
                break;

            case FILE_PATH:
                rtrn = free_filepath((char **)&(ctx->arg_copy_array[i]));
                if(rtrn < 0)
                    output(ERROR, "Can't free filepath\n");
                /* Don't return on errors, just keep looping. */
                break;

            case DIR_PATH:
                rtrn = free_dirpath((char **)&(ctx->arg_copy_array[i]));
                if(rtrn < 0)
                    output(ERROR, "Can't free dirpath\n");
                /* Don't return on errors, just keep looping. */
                break;

            case SOCKET:
                rtrn = free_socket((int32_t *)ctx->arg_copy_array[i]);
                if(rtrn < 0)
                    output(ERROR, "Can't free socket\n");
                /* Don't return on errors, just keep looping. */
                break;
            /* End of resource types. */

            /* Clean this with mem_free_shared(). */
            case VOID_BUF:
                mem_free_shared((void **)&ctx->arg_value_array[i],
                                ctx->arg_size_array[i]);
                break;

            /* Kill the temp process using the copy value
               so that we don't use the mutated value
               in arg_value_index[i]. */
            case PID:
                /*rtrn = kill((pid_t)(*ctx->arg_copy_array[i]), SIGKILL);
                if(rtrn < 0)
                    output(ERROR, "Can't kill child: %s\n", strerror(errno));
                 Don't return on errors, just keep looping. */
                break;

            default:
                /* Free value if non NULL. */
                //mem_free((void **)&ctx->arg_value_array[i]);
                break;
        }
    }

    return (0);
}

static int32_t set_syscall(uint32_t num, struct child_ctx *ctx)
{
    /* Make sure number passed is in bounds. */
    if(num > sys_table->number_of_syscalls - 1)
    {
        output(ERROR, "Syscall number passed is out of bounds\n");
        return (-1);
    }

    /* Set syscall value's. */
    ctx->syscall_number = num;
    ctx->syscall_symbol = sys_table->sys_entry[num]->syscall_symbol;
    ctx->syscall_name = sys_table->sys_entry[num]->syscall_name;
    ctx->need_alarm = sys_table->sys_entry[num]->need_alarm;
    ctx->total_args = sys_table->sys_entry[num]->total_args;
    ctx->had_error = NX_NO;

    return (0);
}

static struct job_ctx *get_job(struct child_ctx *ctx)
{
    struct job_ctx *job = NULL;

    /* Grab message(job) from childs message port(job queue). */
    job = (struct job_ctx *)msg_recv(ctx->msg_port);
    if(job == NULL)
    {
        output(ERROR, "Can't recieve message\n");
        return (NULL);
    }

    return (job);
}

static int32_t process_new_generation(struct job_ctx *job) { return (0); }

static int32_t process_genesis(struct job_ctx *job)
{
    int32_t rtrn = 0;
    struct syscall_entry_shadow *entry = NULL;
    struct child_ctx *ctx = NULL;

    /* Get our child context. */
    ctx = get_child_ctx();
    if(ctx == NULL)
    {
        output(ERROR, "Can't get child context\n");
        exit_child();
    }

    /* Set the return jump so that we can try fuzzing again on a signal. */
    rtrn = setjmp(ctx->return_jump);
    if(rtrn < 0)
    {
        output(ERROR, "Can't set return jump\n");
        exit_child();
    }

    /* Set the syscall that the genetic algo told us to test. */
    rtrn = set_syscall(job->syscall_number, ctx);
    if(rtrn < 0)
    {
        output(ERROR, "Can't pick syscall to test\n");
        exit_child();
    }

    /* Generate arguments for the syscall selected. */
    rtrn = generate_arguments(ctx);
    if(rtrn < 0)
    {
        output(ERROR, "Can't generate arguments\n");
        exit_child();
    }

    entry = get_entry(ctx->syscall_number);
    if(entry == NULL)
    {
        output(ERROR, "Can't get syscall entry\n");
        exit_child();
    }

    /* Log the arguments before we use them, in case we cause a
    kernel panic, so we know what caused the panic. */
    rtrn = log_arguments(ctx->total_args, ctx->syscall_name,
                         ctx->arg_value_array, entry->arg_context_array);
    if(rtrn < 0)
    {
        output(ERROR, "Can't log arguments\n");
        exit_child();
    }

    /* Run the syscall we selected with the arguments we generated and mutated. This call usually
    crashes and causes us to jumb back to the setjmp call above.*/
    rtrn = test_syscall(ctx);
    if(rtrn < 0)
    {
        output(ERROR, "Syscall call failed\n");
        exit_child();
    }

    /* Log return values. 
    rtrn = log_results(obj);
    if(rtrn < 0)
    {
        output(ERROR, "Can't log results\n");
        exit_child();
    } */

    /* If we didn't crash cleanup are mess. If we don't do this the generate
    functions will crash in a hard to understand way. */
    free_old_arguments(ctx);

    return (0);
}

static int32_t do_job(struct job_ctx *job)
{
    /* Figure out what job type is being process. */
    switch((int32_t)job->type)
    {
        case GENESIS:
            return (process_genesis(job));

        case NEW_GENERATION:
            return (process_new_generation(job));

        default:
            output(ERROR, "Unknown job type\n");
            return (-1);
    }
}

NX_NO_RETURN static void start_smart_syscall_child(void)
{
    int32_t rtrn = 0;
    struct child_ctx *ctx = NULL;

    /* Grab our context object. */
    ctx = get_child_ctx();
    if(ctx == NULL)
    {
        output(ERROR, "Can't get child context\n");
        exit_child();
    }

    /* Set the return jump so that we can try fuzzing again on a signal. This 
    is required on some operating systems because they can't clean up old 
    processes fast enough for us. It also alows us to do PRNG seeding and
    probe injection and teardown less often. */
    rtrn = setjmp(ctx->return_jump);
    if(rtrn < 0)
    {
        output(ERROR, "Can't set return jump\n");
        exit_child();
    }

    /* Loop until ctrl-c is pressed by the user. */
    while(ck_pr_load_int(stop) != TRUE)
    {
        struct job_ctx *job = NULL;

        /* Grab a job from the child's message port. */
        job = get_job(ctx);
        if(job == NULL)
        {
            output(ERROR, "Can't get job\n");
            exit_child();
        }

        /* Proccess the orders from the genetic algorithm. */
        rtrn = do_job(job);
        if(rtrn < 0)
        {
            output(ERROR, "Can't do job\n");
            exit_child();
        }
    }

    exit_child();
}

struct child_ctx *get_child_ctx_from_pid(pid_t pid)
{
    uint32_t i;

    for(i = 0; i < number_of_children; i++)
    {
        if(children[i]->pid == pid)
            return (children[i]);
    }

    return (NULL);
}

struct child_ctx *get_child_ctx(void)
{
    int32_t rtrn = 0;
    uint32_t child_number = 0;
    struct child_ctx *child = NULL;

    /* Allocate a child context object. */
    child = mem_alloc(sizeof(struct child_ctx));
    if(child == NULL)
    {
        output(ERROR, "Can't allocate child context\n");
        return (NULL);
    }

    /* Grab the child_ctx index number for this child process. */
    rtrn = get_child_index_number(&child_number);
    if(rtrn < 0)
    {
        output(ERROR, "Can't grab child number\n");
        return (NULL);
    }

    /* Grab child context. */
    child = children[child_number];

    /* Return our child context. */
    return (child);
}

/**
 * This is the fuzzing loop for syscall fuzzing in dumb mode.
 */
NX_NO_RETURN static void start_syscall_child(void)
{
    int32_t rtrn = 0;
    struct syscall_entry_shadow *entry = NULL;
    struct child_ctx *ctx = NULL;

    /* Get our child context. */
    ctx = get_child_ctx();
    if(ctx == NULL)
    {
        output(ERROR, "Can't get child context\n");
        exit_child();
    }

    /* Set the return jump so that we can try fuzzing again on a signal. */
    rtrn = setjmp(ctx->return_jump);
    if(rtrn < 0)
    {
        output(ERROR, "Can't set return jump\n");
        exit_child();
    }

    /* Check to see if we jumped back from the signal handler. */
    if(ctx->did_jump == NX_YES)
    {
        /* Log the results of the last fuzz test. */
        rtrn = log_results(ctx->had_error, ctx->ret_value, ctx->err_value);
        if(rtrn < 0)
        {
            output(ERROR, "Can't log test results\n");
            exit_child();
        }

        /* Clean up our old mess. */
        rtrn = free_old_arguments(ctx);
        if(rtrn < 0)
        {
            output(ERROR, "Can't cleanup old arguments\n");
            exit_child();
        }
    }

    /* Check if we should stop or continue running. */
    while((*stop) != TRUE)
    {
        /* Randomly pick the syscall to test. */
        rtrn = pick_syscall(ctx);
        if(rtrn < 0)
        {
            output(ERROR, "Can't pick syscall to test\n");
            exit_child();
        }
 
        /* Generate arguments for the syscall selected. */
        rtrn = generate_arguments(ctx);
        if(rtrn < 0)
        {
            output(ERROR, "Can't generate arguments\n");
            exit_child();
        }

        /* Mutate the arguments randomly. */
        rtrn = mutate_arguments(ctx);
        if(rtrn < 0)
        {
            output(ERROR, "Can't mutate arguments\n");
            exit_child();
        }

        /* Grab the syscall entry for the syscall we picked. */
        entry = get_entry(ctx->syscall_number);
        if(entry == NULL)
        {
            output(ERROR, "Can't get syscall entry\n");
            exit_child();
        }

        /* Log the arguments before we use them, in case we cause a
        kernel panic, so we know what caused the panic. */
        rtrn = log_arguments(ctx->total_args, ctx->syscall_name,
                             ctx->arg_value_array, entry->arg_context_array);
        if(rtrn < 0)
        {
            output(ERROR, "Can't log arguments\n");
            exit_child();
        }

        /* Run the syscall we selected with the arguments we generated and mutated. This call usually
        crashes and causes us to jumb back to the setjmp call above.*/
        rtrn = test_syscall(ctx);
        if(rtrn < 0)
        {
            output(ERROR, "Syscall call failed\n");
            exit_child();
        }

        rtrn = log_results(ctx->had_error, ctx->ret_value, ctx->err_value);
        if(rtrn < 0)
        {
            output(ERROR, "Can't log test results\n");
            exit_child();
        }

        /* If we didn't crash, cleanup are mess. If we don't do this the generate
        functions will crash in a hard to understand way. */
        free_old_arguments(ctx);
    }

    /* We should not get here, but if we do, exit so we can be restarted. */
    exit_child();
}

static int32_t init_syscall_child(uint32_t i)
{
    int32_t rtrn = 0;

    /* Set the child pid. */
    cas_loop_int32(&children[i]->pid, getpid());

    /* Set up the child signal handlers. */
    setup_child_signal_handler();

    /* If were using a software PRNG we need to seed the PRNG. */
    if(using_hardware_prng() == FALSE)
    {
        /* We got to seed the prng so that the child process trys different syscalls. */
        rtrn = seed_prng();
        if(rtrn < 0)
        {
            output(ERROR, "Can't init syscall\n");
            return (-1);
        }
    }

    /* Check if we are in smart mode. */
    if(mode == TRUE)
    {
        /* Inject probes into the kernel. */
        rtrn = inject_kernel_probes(children[i]->probe_handle);
        if(rtrn < 0)
        {
            output(ERROR, "Can't init child probes\n");
            return (-1);
        }
    }

    /* Increment the running child counter. */
    ck_pr_add_uint(&running_children, 1);

    return (0);
}

NX_NO_RETURN static void start_child_loop(void)
{
    /* If were in dumb mode start the dumb syscall loop. */
    if(mode != TRUE)
    {
        start_syscall_child();
    }

    start_smart_syscall_child();
}

NX_NO_RETURN static void start_child(uint32_t i)
{
    /* Initialize the new syscall child. */
    init_syscall_child(i);

    /* Start the newly created syscall child's main loop. */
    start_child_loop();
}

static int32_t create_syscall_child_proc(msg_port_t port, void *arg)
{
    uint32_t *i = (uint32_t *)arg;

    /* Start child process's loop. */
    start_child((*i));
}

void create_syscall_children(void)
{
    uint32_t i = 0;
    int32_t rtrn = 0;

    /* Walk the child structure array and find the first empty child slot. */
    for(i = 0; i < number_of_children; i++)
    {
        /* If the child does not have a pid of EMPTY let's create a new one. */
        if(children[i]->pid != EMPTY)
            continue;

        msg_port_t port = 0;
        msg_port_t child_port = 0;

        /* Create child process and pass it a message port. */
        rtrn = fork_pass_port(&port, create_syscall_child_proc, &i);
        if(rtrn == 0)
        {
            output(ERROR, "Can't create child process\n");
            return;
        }

        /* Wait for the child to send us it's message port. */
        rtrn = recv_port(port, &child_port);
        if(rtrn < 0)
        {
            output(ERROR, "Can't recieve message\n");
            return;
        }

        return;
    }
}

struct syscall_table_shadow *get_syscall_table(void)
{
    struct syscall_table *syscall_table = NULL;
    struct syscall_table_shadow *shadow_table = NULL;

    /* Grab a copy of the syscall table in on disk format. */
    syscall_table = get_table();
    if(syscall_table == NULL)
    {
        output(STD, "Can't grab syscall table\n");
        return (NULL);
    }

    /* Create a shadow syscall table.*/
    shadow_table = mem_alloc(sizeof(struct syscall_table_shadow));
    if(shadow_table == NULL)
    {
        output(ERROR, "Can't create shadow table\n");
        return (NULL);
    }

    /* Set the number_of_syscalls. */
    shadow_table->number_of_syscalls = syscall_table->number_of_syscalls;

    /* Our loop incrementers. */
    uint32_t i, ii;

    /* Allocate heap memory for the list of syscalls. */
    shadow_table->sys_entry = mem_alloc(shadow_table->number_of_syscalls *
                                        sizeof(struct syscall_entry_shadow *));
    if(shadow_table->sys_entry == NULL)
    {
        output(ERROR, "Can't create entry index\n");
        return (NULL);
    }

    /* This is the entry offset, it one because the entries start at [1] instead of [0]; */
    uint32_t offset = 1;

    uint32_t loops = shadow_table->number_of_syscalls;

    /* Loop for each entry syscall and build a table from the on disk format. */
    for(i = 0; i < loops; i++)
    {
        /* Check if the syscall is OFF, this is usually for syscalls in development. */
        if(syscall_table[i + offset].sys_entry->status == OFF)
        {
            shadow_table->number_of_syscalls--;
            continue;
        }

        /* Create and intialize the const value for a shadow struct. */
        struct syscall_entry_shadow entry_obj = {

            .total_args =
                syscall_table[i + offset].sys_entry->total_args,
            .syscall_name =
                syscall_table[i + offset].sys_entry->syscall_name
        };

        struct syscall_entry_shadow *entry = NULL;

        entry = mem_alloc(sizeof(struct syscall_entry_shadow));
        if(entry == NULL)
        {
            output(ERROR, "Can't create syscall entry\n");
            return (NULL);
        }

        memmove(entry, &entry_obj, sizeof(struct syscall_entry_shadow));

        /* Loop for each arg and set the arg array's. */
        for(ii = 0; ii < entry->total_args; ii++)
        {
            /* Set the get argument function pointers. */
            entry->get_arg_array[ii] =
                syscall_table[i + offset].sys_entry->get_arg_array[ii];

            /* Set argument type context structs. */
            entry->arg_context_array[ii] =
                get_arg_context((enum arg_type)syscall_table[i + 1]
                                    .sys_entry->arg_type_array[ii]);
            if(entry->arg_context_array[ii] == NULL)
            {
                output(ERROR, "Can't get arg context\n");
                return (NULL);
            }
        }

        /* Init atomic value. */
        atomic_init(&entry->status, ON);

        set_test_syscall(entry, syscall_table[i + offset].sys_entry->id);

        /* Set the newly created entry in the index. */
        shadow_table->sys_entry[i] = entry;

    } /* End of loop. */

    /* Check if we have a empty syscall table. */
    if(i == 0)
    {
        output(ERROR, "Empty syscall table\n");
        return (NULL);
    }

    return (shadow_table);
}

/* This function is used to randomly pick the syscall to test. */
int32_t pick_syscall(struct child_ctx *ctx)
{
    /* Use rand_range to pick a number between 0 and the number_of_syscalls. The minus one
    is a hack, get_syscall_table() returns an array - 1 the size of number of syscalls 
    and should be fixed. */
    int32_t rtrn =
        rand_range(sys_table->number_of_syscalls - 1, &ctx->syscall_number);
    if(rtrn < 0)
    {
        output(ERROR, "Can't generate random number\n");
        return (-1);
    }

    /* Set syscall value's. */
    ctx->syscall_symbol =
        sys_table->sys_entry[ctx->syscall_number]->syscall_symbol;
    ctx->syscall_name =
        sys_table->sys_entry[ctx->syscall_number]->syscall_name;
    ctx->need_alarm = sys_table->sys_entry[ctx->syscall_number]->need_alarm;
    ctx->total_args =
        sys_table->sys_entry[ctx->syscall_number]->total_args;
    ctx->had_error = NX_NO;

    return (0);
}

int32_t generate_arguments(struct child_ctx *ctx)
{
    uint32_t i = 0;
    int32_t rtrn = 0;
    uint32_t total_args = ctx->total_args;

    /* Loop for each syscall argument. */
    for(i = 0; i < total_args; i++)
    {
        /* Set the current argument number. */
        ctx->current_arg = i;

        /* Generate the argument. */
        rtrn = sys_table->sys_entry[ctx->syscall_number]->get_arg_array[i](
            &ctx->arg_value_array[i], ctx);
        if(rtrn < 0)
        {
            output(ERROR, "Can't generate arguments for: %s\n",
                   sys_table->sys_entry[ctx->syscall_number]->syscall_name);
            return (-1);
        }

        /* Copy the argument into the argument copy array. */
        memcpy(ctx->arg_copy_array[i], ctx->arg_value_array[i],
               ctx->arg_size_array[i]);
    }

    return (0);
}

static int32_t check_for_failure(int32_t ret_value)
{
    if(ret_value < 0)
        return (-1);

    return (0);
}

int32_t test_syscall(struct child_ctx *ctx)
{
    /* Check if we need to set the alarm for blocking syscalls.  */
    if(ctx->need_alarm == NX_YES)
        alarm(1);

    struct syscall_entry_shadow *entry = get_entry(ctx->syscall_number);
    if(entry == NULL)
    {
        output(ERROR, "Can't get syscall entry\n");
        return (-1);
    }

    /* Do the add before the syscall test because we usually crash on the syscall test
    and we don't wan't to do this in a signal handler. */
    ck_pr_add_uint(test_counter, 1);

    /* Set the time of the syscall test. */
    (void)gettimeofday(&ctx->time_of_syscall, NULL);

    /* Call the syscall with the args generated. */
    ctx->ret_value = entry->test_syscall(ctx->syscall_symbol, ctx->arg_value_array);
    if(check_for_failure(ctx->ret_value) < 0)
    {
        /* If we got here, we had an error, so grab the error string. */
        ctx->err_value = strerror(errno);

        /* Set the error flag so the logging system knows we had an error. */
        ctx->had_error = NX_YES;
    }

    return (0);
}

void kill_all_children(void)
{
    uint32_t i = 0;
    int32_t rtrn = 0;

    for(i = 0; i < number_of_children; i++)
    {
        int32_t pid = ck_pr_load_int(&children[i]->pid);

        rtrn = kill(pid, SIGKILL);
        if(rtrn < 0)
        {
            output(ERROR, "Can't kill child: %s\n", strerror(errno));
            return;
        }
    }

    return;
}

void start_main_syscall_loop(void)
{
    output(STD, "Starting fuzzer\n");

    /* Set up signal handler. */
    setup_signal_handler();

    /* Check if we should stop or continue running. */
    while(ck_pr_load_int(stop) == FALSE)
    {
        /* Check if we have the right number of children processes running, if not create a new ones until we do. */
        if(ck_pr_load_uint(&running_children) < number_of_children)
        {
            /* Create children process. */
            create_syscall_children();
        }
    }

    output(STD, "Exiting main loop\n");
}

static struct child_ctx *init_child_context(void)
{
    struct child_ctx *child = NULL;

    /* Allocate the child context object. */
    child = mem_alloc(sizeof(struct child_ctx));
    if(child == NULL)
    {
        output(ERROR, "Can't create child context\n");
        return (NULL);
    }

    /* Set current arg to zero. */
    child->current_arg = 0;

    /* Create the index where we store the syscall arguments. */
    child->arg_value_array = mem_alloc(ARG_LIMIT * sizeof(uint64_t *));
    if(child->arg_value_array == NULL)
    {
        output(ERROR, "Can't create arg value index: %s\n", strerror(errno));
        return (NULL);
    }

    /* This index tracks the size of the argument generated.  */
    child->arg_size_array = mem_alloc(ARG_LIMIT * sizeof(uint64_t));
    if(child->arg_size_array == NULL)
    {
        output(ERROR, "Can't create arg size index: %s\n", strerror(errno));
        return (NULL);
    }

    child->arg_copy_array = mem_alloc(ARG_LIMIT * sizeof(uint64_t *));
    if(child->arg_copy_array == NULL)
    {
        output(ERROR, "Can't create arg copy index: %s\n", strerror(errno));
        return (NULL);
    }

    /* This is where we store the error string on each syscall test. */
    child->err_value = mem_alloc(1024);
    if(child->err_value == NULL)
    {
        output(ERROR, "err_value: %s\n", strerror(errno));
        return (NULL);
    }

    uint32_t i = 0;

    /* Loop and create the various arrays in the child struct. */
    for(i = 0; i < ARG_LIMIT; i++)
    {
        child->arg_value_array[i] = mem_alloc(ARG_BUF_LEN);
        if(child->arg_value_array[i] == NULL)
        {
            output(ERROR, "Can't create arg value\n");
            return (NULL);
        }

        child->arg_copy_array[i] = mem_alloc(ARG_BUF_LEN);
        if(child->arg_copy_array[i] == NULL)
        {
            output(ERROR, "Can't create arg copy value\n");
            return (NULL);
        }
    }

    return (child);
}

int32_t setup_syscall_module(int32_t *stop_ptr,
                             uint32_t *counter, 
                             int32_t run_mode)
{
    uint32_t i = 0;
    int32_t rtrn = 0;

    /* Set running children to zero. */
    ck_pr_store_uint(&running_children, 0);

    /* Grab the core count of the machine we are on and set the number
    of syscall children to the core count. */
    rtrn = get_core_count(&number_of_children);
    if(rtrn < 0)
    {
        output(ERROR, "Can't get core count\n");
        return (-1);
    }

    /* Allocate the system call table as shared memory. */
    sys_table = mem_alloc(sizeof(struct syscall_table));
    if(sys_table == NULL)
    {
        output(ERROR, "Can't allocate syscall table\n");
        return (-1);
    }

    /* Build and set the syscall table for the system we are on. */
    sys_table = get_syscall_table();
    if(sys_table == NULL)
    {
        output(ERROR, "Can't get syscall table\n");
        return (-1);
    }

    /* Create the child process structures. */
    children = mem_alloc(number_of_children * sizeof(struct child_ctx *));
    if(children == NULL)
    {
        output(ERROR, "Can't create children index.\n");
        return (-1);
    }

    /* Loop for each child and allocate the child context object. */
    for(i = 0; i < number_of_children; i++)
    {
        struct child_ctx *child = NULL;

        /* Create and initialize the child context struct. */
        child = init_child_context();
        if(child == NULL)
        {
            output(ERROR, "Can't init child context\n");
            return (-1);
        }

        /* Set the newly created child context to the children index. */
        children[i] = child;
    }

    /* Set file scope variables. */
    stop = stop_ptr;
    mode = run_mode;
    test_counter = counter;

    return (0);
}
