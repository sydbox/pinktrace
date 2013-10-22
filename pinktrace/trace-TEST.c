/*
 * Copyright (c) 2012, 2013 Ali Polatel <alip@exherbo.org>
 * Based in part upon strace which is:
 *   Copyright (c) 1991, 1992 Paul Kranenburg <pk@cs.few.eur.nl>
 *   Copyright (c) 1993 Branko Lankester <branko@hacktic.nl>
 *   Copyright (c) 1993, 1994, 1995, 1996 Rick Sladkey <jrs@world.std.com>
 *   Copyright (c) 1996-1999 Wichert Akkerman <wichert@cistron.nl>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "pinktrace-check.h"

#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

/*
 * Test whether the kernel support PTRACE_O_TRACECLONE et al options.
 * First fork a new child, call ptrace with PTRACE_SETOPTIONS on it,
 * and then see which options are supported by the kernel.
 */
static void test_trace_clone(void)
{
	pid_t pid, expected_grandchild = 0, found_grandchild = 0;
	const unsigned int test_options = PINK_TRACE_OPTION_CLONE |
					  PINK_TRACE_OPTION_FORK |
					  PINK_TRACE_OPTION_VFORK;

	pid = fork_assert();
	if (pid == 0) {
		trace_me_and_stop();
		if (fork() < 0) {
			perror("fork");
			_exit(2);
		}
		_exit(0);
	}

	LOOP_WHILE_TRUE() {
		int r, status;
		pid_t tracee_pid;

		errno = 0;
		tracee_pid = wait_verbose(&status);
		if (tracee_pid <= 0 && check_echild_or_kill(pid, tracee_pid))
			break;
		if (WIFEXITED(status)) {
			if (WEXITSTATUS(status)) {
				if (tracee_pid != pid)
					kill_save_errno(pid, SIGKILL);
				fail_verbose("unexpected exit status %u",
					     WEXITSTATUS(status));
			}
			continue;
		}
		if (WIFSIGNALED(status)) {
			if (tracee_pid != pid)
				kill_save_errno(pid, SIGKILL);
			fail_verbose("unexpected signal %u", WTERMSIG(status));
		}
		if (!WIFSTOPPED(status)) {
			if (tracee_pid != pid)
				kill_save_errno(tracee_pid, SIGKILL);
			kill_save_errno(pid, SIGKILL);
			fail_verbose("unexpected wait status %#x", status);
		}
		if (tracee_pid != pid) {
			found_grandchild = tracee_pid;
			r = pink_trace_resume(tracee_pid, 0);
			if (r < 0) {
				kill_save_errno(tracee_pid, SIGKILL);
				kill_save_errno(pid, SIGKILL);
				fail_verbose("PTRACE_CONT (errno:%d %s)",
					     -r, strerror(-r));
			}
			continue;
		}
		switch (WSTOPSIG(status)) {
		case SIGSTOP:
			trace_setup_or_kill(pid, test_options);
			break;
		case SIGTRAP:
			if (event_decide_and_print(status) == PINK_EVENT_FORK) {
				unsigned long msg = 0;

				trace_geteventmsg_or_kill(pid, &msg);
				expected_grandchild = msg;
			}
			break;
		}
		trace_syscall_or_kill(pid, 0);
	}

	if (!(expected_grandchild && expected_grandchild == found_grandchild))
		fail_verbose("Test for PINK_TRACE_OPTION_CLONE failed");
}

/*
 * Test whether the kernel support PTRACE_O_TRACESYSGOOD.
 * First fork a new child, call ptrace(PTRACE_SETOPTIONS) on it,
 * and then see whether it will stop with (SIGTRAP | 0x80).
 *
 * Use of this option enables correct handling of user-generated SIGTRAPs,
 * and SIGTRAPs generated by special instructions such as int3 on x86:
 * _start:	.globl	_start
 *		int3
 *		movl	$42, %ebx
 *		movl	$1, %eax
 *		int	$0x80
 * (compile with: "gcc -nostartfiles -nostdlib -o int3 int3.S")
 */
static void test_trace_sysgood(void)
{
	const unsigned int test_options = PINK_TRACE_OPTION_SYSGOOD;
	pid_t pid;
	bool it_worked = false;

	pid = fork_assert();
	if (pid == 0) {
		trace_me_and_stop();
		_exit(0);
	}

	LOOP_WHILE_TRUE() {
		int status;
		pid_t tracee_pid;

		tracee_pid = wait_verbose(&status);
		if (tracee_pid <= 0 && check_echild_or_kill(pid, tracee_pid))
			break;
		if (check_exit_code_or_fail(status, 0))
			break;
		check_signal_or_fail(status, 0);
		check_stopped_or_kill(tracee_pid, status);
		if (WSTOPSIG(status) == SIGSTOP) {
			trace_setup_or_kill(pid, test_options);
		}
		if (WSTOPSIG(status) == (SIGTRAP | 0x80)) {
			it_worked = true;
		}
		trace_syscall_or_kill(pid, 0);
	}

	if (!it_worked)
		fail_verbose("Test for PINK_TRACE_OPTION_SYSGOOD failed");
}

/* Test whether the kernel supports PTRACE_O_TRACEEXEC */
static void test_trace_exec(void)
{
	const unsigned int test_options = PINK_TRACE_OPTION_EXEC;
	pid_t pid;
	bool it_worked = false;

	pid = fork_assert();
	if (pid == 0) {
		char *const argv[] = { NULL };
		trace_me_and_stop();
		execve("/bin/true", argv, environ);
		_exit(1);
	}

	LOOP_WHILE_TRUE() {
		int status;
		pid_t tracee_pid;
		unsigned long old_pid = 0;

		tracee_pid = wait_verbose(&status);
		if (tracee_pid <= 0 && check_echild_or_kill(pid, tracee_pid))
			break;
		if (check_exit_code_or_fail(status, 0))
			break;
		check_signal_or_fail(status, 0);
		check_stopped_or_kill(tracee_pid, status);
		if (WSTOPSIG(status) == SIGSTOP) {
			trace_setup_or_kill(pid, test_options);
		}
		if (WSTOPSIG(status) == SIGTRAP) {
			if (event_decide_and_print(status) == PINK_EVENT_EXEC) {
				if (os_release < KERNEL_VERSION(3,0,0)) {
					it_worked = true;
					kill(pid, SIGKILL);
					break;
				}
				trace_geteventmsg_or_kill(pid, &old_pid);
				if ((pid_t)old_pid != pid) {
					kill(pid, SIGKILL);
					fail_verbose("PINK_TRACE_OPTION_EXEC works but can't tell the old pid");
				}
				it_worked = true;
				kill(pid, SIGKILL);
				break;
			}
		}
		trace_syscall_or_kill(pid, 0);
	}

	if (!it_worked)
		fail_verbose("Test for PINK_TRACE_OPTION_EXEC failed");
}

static void test_fixture_trace(void) {
	test_fixture_start();
	run_test(test_trace_clone);
	run_test(test_trace_sysgood);
	run_test(test_trace_exec);
	test_fixture_end();
}

void test_suite_trace(void) {
	test_fixture_trace();
}