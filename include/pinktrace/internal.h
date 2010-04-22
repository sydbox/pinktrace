/* vim: set cino= fo=croql sw=8 ts=8 sts=0 noet cin fdm=syntax : */

/*
 * Copyright (c) 2010 Ali Polatel <alip@exherbo.org>
 *
 * This file is part of Pink's Tracing Library. pinktrace is free software; you
 * can redistribute it and/or modify it under the terms of the GNU Lesser
 * General Public License version 2.1, as published by the Free Software
 * Foundation.
 *
 * pinktrace is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef PINKTRACE_GUARD_INTERNAL_H
#define PINKTRACE_GUARD_INTERNAL_H 1

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include <stdbool.h>
#include <sys/types.h>
#include <sys/ptrace.h>
#include <sys/wait.h>

#ifdef HAVE_SYS_REG_H
#include <sys/reg.h>
#endif /*  HAVE_SYS_REG_H */

/* We need additional hackery on IA64 to include linux/ptrace.h. */
#if defined(IA64)
#ifdef HAVE_STRUCT_IA64_FPREG
#define ia64_fpreg XXX_ia64_fpreg
#endif /* HAVE_STRUCT_IA64_FPREG */
#ifdef HAVE_STRUCT_PT_ALL_USER_REGS
#define pt_all_user_regs XXX_pt_all_user_regs
#endif /* HAVE_STRUCT_PT_ALL_USER_REGS */
#endif /* defined(IA64) */
#include <linux/ptrace.h>
#if defined(IA64)
#undef ia64_fpreg
#undef pt_all_user_regs
#endif /* defined(IA64) */

#define MAX_ARGS	6

#include <pinktrace/context.h>
#include <pinktrace/error.h>
#include <pinktrace/handler.h>
#include <pinktrace/step.h>

struct pink_context
{
	bool attach;
	int options;
	pid_t eldest;
	pink_error_t error;
	pink_step_t step;
};

struct pink_event_handler
{
	/* Context */
	pink_context_t *ctx;

	/* Signal callbacks */
	pink_event_handler_signal_t cb_stop;
	pink_event_handler_signal_t cb_genuine;
	pink_event_handler_signal_t cb_unknown;

	/* Ptrace callbacks */
	pink_event_handler_ptrace_t cb_syscall;
	pink_event_handler_ptrace_t cb_fork;
	pink_event_handler_ptrace_t cb_vfork;
	pink_event_handler_ptrace_t cb_clone;
	pink_event_handler_ptrace_t cb_exec;
	pink_event_handler_ptrace_t cb_vfork_done;
	pink_event_handler_ptrace_t cb_exit;

	/* Exit callbacks */
	pink_event_handler_exit_t cb_exit_genuine;
	pink_event_handler_exit_t cb_exit_signal;

	/* Error handler */
	pink_event_handler_error_t cb_error;

	/* User data */
	void *userdata_stop;
	void *userdata_genuine;
	void *userdata_unknown;
	void *userdata_syscall;
	void *userdata_fork;
	void *userdata_vfork;
	void *userdata_clone;
	void *userdata_exec;
	void *userdata_vfork_done;
	void *userdata_exit;
	void *userdata_exit_genuine;
	void *userdata_exit_signal;
	void *userdata_error;
};

const char *
pink_name_syscall_nobitness(long scno);

const char *
pink_name_syscall32(long scno);

const char *
pink_name_syscall64(long scno);

#endif /* !PINKTRACE_GUARD_INTERNAL_H */
