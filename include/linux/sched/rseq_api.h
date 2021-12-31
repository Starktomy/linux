/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SCHED_RSEQ_API_H
#define _LINUX_SCHED_RSEQ_API_H

#include <linux/sched/thread_info_api.h>
#include <linux/signal_types.h>
#include <linux/sched/per_task.h>
#include <linux/rseq.h>

#include <linux/sched.h>
#include <linux/preempt.h>

#ifdef CONFIG_RSEQ

DECLARE_PER_TASK(struct rseq __user *, rseq);

/*
 * Map the event mask on the user-space ABI enum rseq_cs_flags
 * for direct mask checks.
 */
enum rseq_event_mask_bits {
	RSEQ_EVENT_PREEMPT_BIT	= RSEQ_CS_FLAG_NO_RESTART_ON_PREEMPT_BIT,
	RSEQ_EVENT_SIGNAL_BIT	= RSEQ_CS_FLAG_NO_RESTART_ON_SIGNAL_BIT,
	RSEQ_EVENT_MIGRATE_BIT	= RSEQ_CS_FLAG_NO_RESTART_ON_MIGRATE_BIT,
};

enum rseq_event_mask {
	RSEQ_EVENT_PREEMPT	= (1U << RSEQ_EVENT_PREEMPT_BIT),
	RSEQ_EVENT_SIGNAL	= (1U << RSEQ_EVENT_SIGNAL_BIT),
	RSEQ_EVENT_MIGRATE	= (1U << RSEQ_EVENT_MIGRATE_BIT),
};

static inline void rseq_set_notify_resume(struct task_struct *t)
{
	if (per_task(t, rseq))
		set_tsk_thread_flag(t, TIF_NOTIFY_RESUME);
}

void __rseq_handle_notify_resume(struct ksignal *sig, struct pt_regs *regs);

static inline void rseq_handle_notify_resume(struct ksignal *ksig,
					     struct pt_regs *regs)
{
	if (per_task(current, rseq))
		__rseq_handle_notify_resume(ksig, regs);
}

static inline void rseq_signal_deliver(struct ksignal *ksig,
				       struct pt_regs *regs)
{
	preempt_disable();
	__set_bit(RSEQ_EVENT_SIGNAL_BIT, &current->rseq_event_mask);
	preempt_enable();
	rseq_handle_notify_resume(ksig, regs);
}

/* rseq_preempt() requires preemption to be disabled. */
static inline void rseq_preempt(struct task_struct *t)
{
	__set_bit(RSEQ_EVENT_PREEMPT_BIT, &t->rseq_event_mask);
	rseq_set_notify_resume(t);
}

/* rseq_migrate() requires preemption to be disabled. */
static inline void rseq_migrate(struct task_struct *t)
{
	__set_bit(RSEQ_EVENT_MIGRATE_BIT, &t->rseq_event_mask);
	rseq_set_notify_resume(t);
}

/*
 * If parent process has a registered restartable sequences area, the
 * child inherits. Unregister rseq for a clone with CLONE_VM set.
 */
static inline void rseq_fork(struct task_struct *t, unsigned long clone_flags)
{
	if (clone_flags & CLONE_VM) {
		per_task(t, rseq) = NULL;
		t->rseq_sig = 0;
		t->rseq_event_mask = 0;
	} else {
		per_task(t, rseq) = per_task(current, rseq);
		t->rseq_sig = current->rseq_sig;
		t->rseq_event_mask = current->rseq_event_mask;
	}
}

static inline void rseq_execve(struct task_struct *t)
{
	per_task(t, rseq) = NULL;
	t->rseq_sig = 0;
	t->rseq_event_mask = 0;
}

#else

static inline void rseq_set_notify_resume(struct task_struct *t)
{
}
static inline void rseq_handle_notify_resume(struct ksignal *ksig,
					     struct pt_regs *regs)
{
}
static inline void rseq_signal_deliver(struct ksignal *ksig,
				       struct pt_regs *regs)
{
}
static inline void rseq_preempt(struct task_struct *t)
{
}
static inline void rseq_migrate(struct task_struct *t)
{
}
static inline void rseq_fork(struct task_struct *t, unsigned long clone_flags)
{
}
static inline void rseq_execve(struct task_struct *t)
{
}

#endif

#ifdef CONFIG_DEBUG_RSEQ

void rseq_syscall(struct pt_regs *regs);

#else

static inline void rseq_syscall(struct pt_regs *regs)
{
}

#endif

#endif /* _LINUX_SCHED_RSEQ_API_H */
