/*
 * Copyright 2013, winocm. <winocm@icloud.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 *   Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 *
 *   Redistributions in binary form must reproduce the above copyright notice, this
 *   list of conditions and the following disclaimer in the documentation and/or
 *   other materials provided with the distribution.
 *
 *   If you are going to use this software in any form that does not involve
 *   releasing the source to this project or improving it, let me know beforehand.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Thread status routines for ARM.
 */

#include <mach_rt.h>
#include <mach_debug.h>
#include <mach_ldebug.h>

#include <mach/kern_return.h>
#include <mach/mach_traps.h>
#include <mach/thread_status.h>
#include <mach/vm_param.h>

#include <kern/counters.h>
#include <kern/cpu_data.h>
#include <kern/mach_param.h>
#include <kern/task.h>
#include <kern/thread.h>
#include <kern/sched_prim.h>
#include <kern/misc_protos.h>
#include <kern/assert.h>
#include <kern/debug.h>
#include <kern/spl.h>
#include <kern/kalloc.h>
#include <kern/syscall_sw.h>
#include <ipc/ipc_port.h>
#include <vm/vm_kern.h>
#include <vm/pmap.h>

#define USRSTACK    0x2FE00000

#define kprintf(args...)

/*
 * thread_userstack:
 *
 * Return the user stack pointer from the machine
 * dependent thread state info.
 */
kern_return_t thread_userstack(thread_t thread, int flavor,
                               thread_state_t tstate, unsigned int count,
                               mach_vm_offset_t * user_stack, int *customstack)
{
    struct arm_thread_state *state;

    if (!(*user_stack))
        *user_stack = USRSTACK;

    switch (flavor) {
    case ARM_THREAD_STATE:
        if (count < ARM_THREAD_STATE_COUNT)
            return (KERN_INVALID_ARGUMENT);

        state = (struct arm_thread_state *) tstate;
        *user_stack = state->sp ? state->sp : USRSTACK;
        break;
    default:
        return (KERN_INVALID_ARGUMENT);
    }

    return (KERN_SUCCESS);
}

/**
 * sanitise_cpsr
 *
 * Clear the bad bits off the CPSR and set ModeBits to 0x10 (user mode)
 */
uint32_t sanitise_cpsr(uint32_t cpsr)
{
    uint32_t final_cpsr;

    final_cpsr = (cpsr) & ~(1 << 9);  /* Data endianness */
    final_cpsr &= ~(1 << 7);          /* IRQ */
    final_cpsr &= ~(1 << 6);          /* FIQ */
    final_cpsr &= 0xFFFFFFE0;         /* Mode bits */
    final_cpsr |= 0x10;               /* Set user mode */

    return (final_cpsr);
}

/**
 * thread_entrypoint
 *
 * Set the current thread entry point.
 */
kern_return_t thread_entrypoint(thread_t thread, int flavor,
                                thread_state_t tstate, unsigned int count,
                                mach_vm_offset_t * entry_point)
{
    struct arm_thread_state *state;

    /*
     * Set a default.
     */
    if (*entry_point == 0)
        *entry_point = VM_MIN_ADDRESS;

    switch (flavor) {
    case ARM_THREAD_STATE:
        if (count < ARM_THREAD_STATE_COUNT)
            return (KERN_INVALID_ARGUMENT);
        state = (struct arm_thread_state *) tstate;
        *entry_point = state->pc ? state->pc : VM_MIN_ADDRESS;
        break;
    default:
        return (KERN_INVALID_ARGUMENT);
    }

    return (KERN_SUCCESS);
}

/**
 * thread_userstackdefault
 *
 * Set the default user stack.
 */
kern_return_t thread_userstackdefault(thread_t thread,
                                      mach_vm_offset_t * default_user_stack)
{
    *default_user_stack = USRSTACK;

    return (KERN_SUCCESS);
}

/*
 *  thread_getstatus:
 *
 *  Get the status of the specified thread.
 */
kern_return_t machine_thread_get_state(thread_t thr_act, thread_flavor_t flavor,
                                       thread_state_t tstate,
                                       mach_msg_type_number_t * count)
{
    switch (flavor) {
    case THREAD_STATE_FLAVOR_LIST:
        {
            if (*count < 3)
                return (KERN_INVALID_ARGUMENT);

            tstate[0] = ARM_THREAD_STATE;
            tstate[1] = ARM_VFP_STATE;
            tstate[2] = ARM_EXCEPTION_STATE;
            *count = 3;

            break;
        }

    case THREAD_STATE_FLAVOR_LIST_NEW:
        {
            if (*count < 4)
                return (KERN_INVALID_ARGUMENT);

            tstate[0] = ARM_THREAD_STATE;
            tstate[1] = ARM_VFP_STATE;
            tstate[2] = ARM_EXCEPTION_STATE;
            tstate[3] = ARM_DEBUG_STATE;

            *count = 4;

            break;
        }

    case ARM_THREAD_STATE:
        {
            struct arm_thread_state *state;
            struct arm_thread_state *saved_state;

            if (*count < ARM_THREAD_STATE_COUNT)
                return (KERN_INVALID_ARGUMENT);

            state = (struct arm_thread_state *) tstate;
            saved_state = (struct arm_thread_state *) thr_act->machine.uss;

            state->r[0] = saved_state->r[0];
            state->r[1] = saved_state->r[1];
            state->r[2] = saved_state->r[2];
            state->r[3] = saved_state->r[3];
            state->r[4] = saved_state->r[4];
            state->r[5] = saved_state->r[5];
            state->r[6] = saved_state->r[6];
            state->r[7] = saved_state->r[7];
            state->r[8] = saved_state->r[8];
            state->r[9] = saved_state->r[9];
            state->r[10] = saved_state->r[10];
            state->r[11] = saved_state->r[11];
            state->r[12] = saved_state->r[12];

            state->sp = saved_state->sp;
            state->lr = saved_state->lr;
            state->pc = saved_state->pc;
            state->cpsr = saved_state->cpsr;

            *count = ARM_THREAD_STATE_COUNT;

            break;
        }
    case ARM_VFP_STATE:
        {
            int i;
            struct arm_vfp_state *state;
            struct arm_vfp_state *saved_state;

            if (*count < ARM_VFP_STATE_COUNT)
                return (KERN_INVALID_ARGUMENT);

            state = (struct arm_vfp_state *) tstate;
            saved_state = (struct arm_vfp_state *) &thr_act->machine.vfp_regs;

            for (i = 0; i < 64; i++)
                state->r[i] = saved_state->r[i];
            state->fpscr = saved_state->fpscr;

            *count = ARM_VFP_STATE_COUNT;

            break;
        }
    case ARM_EXCEPTION_STATE:
        {
            struct arm_exception_state *state;
            struct arm_exception_state *saved_state;

            if (*count < ARM_EXCEPTION_STATE_COUNT)
                return (KERN_INVALID_ARGUMENT);

            state = (struct arm_exception_state *)tstate;
            saved_state = (struct arm_exception_state *) &thr_act->machine.es;

            state->fsr = saved_state->fsr;
            state->far = saved_state->far;
            state->exception = saved_state->exception;

            *count = ARM_EXCEPTION_STATE_COUNT;

            break;
        }
    case ARM_DEBUG_STATE:
        {
            break;
        }
    default:
        return (KERN_INVALID_ARGUMENT);
    }

    return (KERN_SUCCESS);
}

/**
 * machine_thread_set_state
 *
 * Set the current thread state.
 */
kern_return_t machine_thread_set_state(thread_t thread, thread_flavor_t flavor,
                                       thread_state_t tstate,
                                       mach_msg_type_number_t count)
{
    /*
     * Validate the count before we do anything else.
     */
    switch (flavor) {
    case ARM_THREAD_STATE:
        if (count < ARM_THREAD_STATE_COUNT) {   /* Is it too short? */
            return (KERN_INVALID_ARGUMENT);     /* Yeah, just leave... */
        }

        break;
    case ARM_DEBUG_STATE:
        if (count < ARM_DEBUG_STATE_COUNT) {    /* Is it too short? */
            return (KERN_INVALID_ARGUMENT);     /* Yeah, just leave... */
        }

        break;
    case ARM_VFP_STATE:
        if (count < ARM_VFP_STATE_COUNT) {      /* Is it too short? */
            return (KERN_INVALID_ARGUMENT);     /* Yeah, just leave... */
        }

        break;
    case ARM_EXCEPTION_STATE:
        if (count < ARM_EXCEPTION_STATE_COUNT) {    /* Is it too short? */
            return (KERN_INVALID_ARGUMENT);         /* Yeah, just leave... */
        }

        break;
    default:
        return (KERN_INVALID_ARGUMENT);
    }

    /*
     * Now set user registers.
     */
    assert(thread != NULL);
    assert(tstate);

    switch (flavor) {
    case ARM_THREAD_STATE:
        {
            struct arm_thread_state *state;
            struct arm_thread_state *new_state;

            new_state = (struct arm_thread_state *) tstate;
            state = (struct arm_thread_state *) &thread->machine.user_regs;

            state->r[0] = new_state->r[0];
            state->r[1] = new_state->r[1];
            state->r[2] = new_state->r[2];
            state->r[3] = new_state->r[3];
            state->r[4] = new_state->r[4];
            state->r[5] = new_state->r[5];
            state->r[6] = new_state->r[6];
            state->r[7] = new_state->r[7];
            state->r[8] = new_state->r[8];
            state->r[9] = new_state->r[9];
            state->r[10] = new_state->r[10];
            state->r[11] = new_state->r[11];
            state->r[12] = new_state->r[12];

            state->sp = new_state->sp;
            state->lr = new_state->lr;
            state->pc = new_state->pc;
            state->cpsr = sanitise_cpsr(new_state->cpsr);

            break;
        }
    case ARM_VFP_STATE:
        {
            int i;
            struct arm_vfp_state *state;
            struct arm_vfp_state *new_state;

            new_state = (struct arm_vfp_state *) tstate;
            state = (struct arm_vfp_state *) &thread->machine.vfp_regs;

            for (i = 0; i < 64; i++)
                state->r[i] = new_state->r[i];
            state->fpscr = new_state->fpscr;

            break;
        }
    case ARM_EXCEPTION_STATE:
        {
            struct arm_exception_state *state;
            struct arm_exception_state *new_state;

            new_state = (struct arm_exception_state *) tstate;
            state = (struct arm_exception_state *) &thread->machine.es;

            state->fsr = new_state->fsr;
            state->far = new_state->far;
            state->exception = new_state->exception;

            break;
        }
    case ARM_DEBUG_STATE:
        {
            break;
        }
    default:
        return (KERN_INVALID_ARGUMENT);
    }

    return (KERN_SUCCESS);
}

/**
 * thread_setuserstack
 *
 * Set a sepcified user stack for sp.
 */
void thread_setuserstack(thread_t thread, mach_vm_address_t user_stack)
{
    assert(thread);
    thread->machine.user_regs.sp = CAST_DOWN(unsigned int, user_stack);

    return;
}

/**
 * thread_adjuserstack
 *
 * Decrement/increment user stack sp by adj amount.
 */
uint64_t thread_adjuserstack(thread_t thread, int adj)
{
    assert(thread);
    thread->machine.user_regs.sp += adj;

    return (thread->machine.user_regs.sp);
}

/**
 * thread_setentrypoint
 *
 * Set the user pc/entrypoint.
 */
void thread_setentrypoint(thread_t thread, uint32_t entry)
{
    assert(thread);
    thread->machine.user_regs.pc = entry;

    return;
}

/**
 * thread_set_parent
 *
 * Set the parent owner of the specified thread.
 */
void thread_set_parent(thread_t parent, int pid)
{
    struct arm_thread_state *iss;
    iss = parent->machine.uss;
    assert(iss);

    iss->r[0] = pid;
    iss->r[1] = 0;

    return;
}

struct arm_act_context {
    arm_saved_state_t ss;
    arm_vfp_state_t fs;
};

/**
 * act_thread_csave
 *
 * Save the current thread context, used for the internal uthread structure.
 */
void *act_thread_csave(void)
{
    kern_return_t kret;
    mach_msg_type_number_t val;
    thread_t thr_act = current_thread();

    struct arm_act_context *ic;

    ic = (struct arm_act_context *)kalloc(sizeof(struct arm_act_context));

    if (ic == NULL)
        return(NULL);

    val = ARM_THREAD_STATE_COUNT;
    if (machine_thread_get_state(thr_act, ARM_THREAD_STATE, (thread_state_t) &ic->ss, &val) != KERN_SUCCESS) {
        kfree(ic, sizeof(struct arm_act_context));
        return(NULL);
    }

    val = ARM_VFP_STATE_COUNT;
    if (machine_thread_get_state(thr_act, ARM_THREAD_STATE, (thread_state_t) &ic->fs, &val) != KERN_SUCCESS) {
        kfree(ic, sizeof(struct arm_act_context));
        return(NULL);
    }

    return (ic);
}

/**
 * act_thread_catt
 *
 * Restore the current thread context, used for the internal uthread structure.
 */
void act_thread_catt(void *ctx)
{
    kern_return_t kret;
    thread_t thr_act = current_thread();

    struct arm_act_context *ic;

    if (ctx == NULL)
        return;

    ic = (struct arm_act_context *)ctx;

    if (machine_thread_set_state(thr_act, ARM_THREAD_STATE, (thread_state_t) &ic->ss, ARM_THREAD_STATE_COUNT) == KERN_SUCCESS)
        (void)machine_thread_set_state(thr_act, ARM_VFP_STATE, (thread_state_t) &ic->fs, ARM_VFP_STATE_COUNT);

    kfree(ic, sizeof(struct arm_act_context));

    return;
}


/**
 * thread_set_child
 *
 * Set the child thread, used in forking.
 */
void thread_set_child(thread_t child, int pid)
{
    assert(child->machine.uss == &child->machine.user_regs);
    child->machine.uss->r[0] = pid;
    child->machine.uss->r[1] = 1;

    return;
}

/**
 * thread_set_wq_state32
 *
 * Set the thread state (used for psynch support).
 */
void thread_set_wq_state32(thread_t thread, thread_state_t tstate)
{
    arm_thread_state_t *state;
    arm_saved_state_t *saved_state;
    thread_t curth = current_thread();
    spl_t s = 0;

    saved_state = thread->machine.uss;
    assert(thread->machine.uss == &thread->machine.user_regs);

    state = (arm_thread_state_t *) tstate;

    if (curth != thread) {
        s = splsched();
        thread_lock(thread);
    }

    bzero(saved_state, sizeof(arm_thread_state_t));
    saved_state->r[0] = state->r[0];
    saved_state->r[1] = state->r[1];
    saved_state->r[2] = state->r[2];
    saved_state->r[3] = state->r[3];
    saved_state->r[4] = state->r[4];
    saved_state->r[5] = state->r[5];

    saved_state->sp = state->sp;
    saved_state->lr = state->lr;
    saved_state->pc = state->pc;
    saved_state->cpsr = sanitise_cpsr(state->cpsr);

    if (curth != thread) {
        thread_unlock(thread);
        splx(s);
    }

    return;
}
