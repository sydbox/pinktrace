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
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>

static const unsigned int test_options = PINK_TRACE_OPTION_SYSGOOD;

/*
 * Test whether reading system call number works with OPTION_SYSGOOD.
 * First fork a new child and call syscall(SYS_getpid), call
 * ptrace(PTRACE_SETOPTIONS) on it, and then when it stops with
 * (SIGTRAP|0x80) call pink_read_syscall().
 *
 * Note: we don't call getpid() here but use syscall() instead because C
 * libraries like glibc may cache the result of getpid() thus returning without
 * calling the actual system call.
 */
START_TEST(TEST_read_syscall)
{
	pid_t pid;
	struct pink_process *current;
	bool it_worked = false;
	long sys_getpid;

	sys_getpid = pink_lookup_syscall("getpid", PINK_ABI_DEFAULT);
	if (sys_getpid == -1)
		fail_verbose("don't know the syscall number of getpid()");

	pid = fork_assert();
	if (pid == 0) {
		trace_me_and_stop();
		syscall(sys_getpid); /* glibc may cache getpid() */
		_exit(0);
	}
	process_alloc_or_kill(pid, &current);

	LOOP_WHILE_TRUE() {
		int status;
		pid_t tracee_pid;
		long sysnum;

		tracee_pid = wait_verbose(&status);
		if (tracee_pid <= 0 && check_echild_or_kill(pid, tracee_pid))
			break;
		if (check_exit_code_or_fail(status, 0))
			break;
		check_signal_or_fail(status, 0);
		check_stopped_or_kill(pid, status);
		if (WSTOPSIG(status) == SIGSTOP) {
			trace_setup_or_kill(pid, test_options);
		} else if (WSTOPSIG(status) == (SIGTRAP|0x80)) {
			process_update_regset_or_kill(current);
			read_syscall_or_kill(current, &sysnum);
			check_syscall_equal_or_kill(pid, sysnum, sys_getpid);
			it_worked = true;
			kill(pid, SIGKILL);
			break;
		}
		trace_syscall_or_kill(pid, 0);
	}

	if (!it_worked)
		fail_verbose("Test for reading system call number"
				" with PINK_TRACE_OPTION_SYSGOOD failed");
}
END_TEST

/*
 * Test whether reading syscall return value works for success.
 * Fork a child and call getpid() which should always return success.
 * Check for the system call return value from parent.
 */
START_TEST(TEST_read_retval_good)
{
	pid_t pid;
	struct pink_process *current;
	bool it_worked = false;
	bool insyscall = false;
	long sys_getpid;

	sys_getpid = pink_lookup_syscall("getpid", PINK_ABI_DEFAULT);
	if (sys_getpid == -1)
		fail_verbose("don't know the syscall number of getpid()");

	pid = fork_assert();
	if (pid == 0) {
		trace_me_and_stop();
		syscall(sys_getpid); /* glibc may cache getpid() */
		_exit(0);
	}
	process_alloc_or_kill(pid, &current);

	LOOP_WHILE_TRUE() {
		int status;
		pid_t tracee_pid;
		int error = 0;
		long rval, sysnum;

		tracee_pid = wait_verbose(&status);
		if (tracee_pid <= 0 && check_echild_or_kill(pid, tracee_pid))
			break;
		if (check_exit_code_or_fail(status, 0))
			break;
		check_signal_or_fail(status, 0);
		check_stopped_or_kill(pid, status);
		if (WSTOPSIG(status) == SIGSTOP) {
			trace_setup_or_kill(pid, test_options);
		} else if (WSTOPSIG(status) == (SIGTRAP|0x80)) {
			if (!insyscall) {
				process_update_regset_or_kill(current);
				read_syscall_or_kill(current, &sysnum);
				check_syscall_equal_or_kill(pid, sysnum, sys_getpid);
				insyscall = true;
			} else {
				process_update_regset_or_kill(current);
				read_retval_or_kill(current, &rval, &error);
				check_retval_equal_or_kill(pid, rval, pid, error, 0);
				it_worked = true;
				kill(pid, SIGKILL);
				break;
			}
		}
		trace_syscall_or_kill(pid, 0);
	}

	if (!it_worked)
		fail_verbose("Test for reading success return value failed");
}
END_TEST

/*
 * Test whether reading syscall return value works for failure.
 * Fork a child and call open(NULL, 0);
 * Check for -EFAULT error condition.
 */
START_TEST(TEST_read_retval_fail)
{
	pid_t pid;
	struct pink_process *current;
	bool it_worked = false;
	bool insyscall = false;
	long sys_open;

	sys_open = pink_lookup_syscall("open", PINK_ABI_DEFAULT);
	if (sys_open == -1)
		fail_verbose("don't know the syscall number of open()");

	pid = fork_assert();
	if (pid == 0) {
		trace_me_and_stop();
		syscall(sys_open, 0, 0);
		_exit(0);
	}
	process_alloc_or_kill(pid, &current);

	LOOP_WHILE_TRUE() {
		int status;
		pid_t tracee_pid;
		int error = 0;
		long rval, sysnum;

		tracee_pid = wait_verbose(&status);
		if (tracee_pid <= 0 && check_echild_or_kill(pid, tracee_pid))
			break;
		if (check_exit_code_or_fail(status, 0))
			break;
		check_signal_or_fail(status, 0);
		check_stopped_or_kill(pid, status);
		if (WSTOPSIG(status) == SIGSTOP) {
			trace_setup_or_kill(pid, test_options);
		} else if (WSTOPSIG(status) == (SIGTRAP|0x80)) {
			if (!insyscall) {
				process_update_regset_or_kill(current);
				read_syscall_or_kill(current, &sysnum);
				check_syscall_equal_or_kill(pid, sysnum, sys_open);
				insyscall = true;
			} else {
				process_update_regset_or_kill(current);
				read_retval_or_kill(current, &rval, &error);
				check_retval_equal_or_kill(pid, rval, -1, error, EFAULT);
				it_worked = true;
				kill(pid, SIGKILL);
				break;
			}
		}
		trace_syscall_or_kill(pid, 0);
	}

	if (!it_worked)
		fail_verbose("Test for reading error return value failed");
}
END_TEST

/*
 * Test whether reading syscall arguments works.
 * First fork a new child, call syscall(PINK_SYSCALL_INVALID, ...) with
 * expected arguments and then check whether they are read correctly.
 */
START_TEST(TEST_read_argument)
{
	pid_t pid;
	struct pink_process *current;
	bool it_worked = false;
	int arg_index = _i;
	long expval = 0xbad;

	pid = fork_assert();
	if (pid == 0) {
		pid = getpid();
		trace_me_and_stop();
		switch (arg_index) {
		case 0: syscall(PINK_SYSCALL_INVALID, expval, 0, 0, 0, -1, 0); break;
		case 1: syscall(PINK_SYSCALL_INVALID, 0, expval, 0, 0, -1, 0); break;
		case 2: syscall(PINK_SYSCALL_INVALID, 0, 0, expval, 0, -1, 0); break;
		case 3: syscall(PINK_SYSCALL_INVALID, 0, 0, 0, expval, -1, 0); break;
		case 4: syscall(PINK_SYSCALL_INVALID, 0, 0, 0, 0, expval, 0);  break;
		case 5: syscall(PINK_SYSCALL_INVALID, 0, 0, 0, 0, -1, expval); break;
		default: _exit(1);
		}
		_exit(0);
	}
	process_alloc_or_kill(pid, &current);

	LOOP_WHILE_TRUE() {
		int status;
		pid_t tracee_pid;
		long argval, sysnum;

		tracee_pid = wait_verbose(&status);
		if (tracee_pid <= 0 && check_echild_or_kill(pid, tracee_pid))
			break;
		if (check_exit_code_or_fail(status, 0))
			break;
		check_signal_or_fail(status, 0);
		check_stopped_or_kill(pid, status);
		if (WSTOPSIG(status) == SIGSTOP) {
			trace_setup_or_kill(pid, test_options);
		} else if (WSTOPSIG(status) == (SIGTRAP|0x80)) {
			process_update_regset_or_kill(current);
			read_syscall_or_kill(current, &sysnum);
			check_syscall_equal_or_kill(pid, sysnum, PINK_SYSCALL_INVALID);
			read_argument_or_kill(current, arg_index, &argval);
			check_argument_equal_or_kill(pid, argval, expval);
			it_worked = true;
			kill(pid, SIGKILL);
			break;
		}
		trace_syscall_or_kill(pid, 0);
	}

	if (!it_worked)
		fail_verbose("Test for reading syscall argument %d failed", arg_index);
}
END_TEST

/*
 * Test whether reading tracee's address space works.
 * First fork a new child, call syscall(PINK_SYSCALL_INVALID, ...) with
 * a filled 'struct stat' and then check whether it's read correctly.
 */
START_TEST(TEST_read_vm_data)
{
	pid_t pid;
	struct pink_process *current;
	bool it_worked = false;
	int arg_index = _i;
	char expstr[] = "pinktrace";
	char newstr[sizeof(expstr)];

	pid = fork_assert();
	if (pid == 0) {
		pid = getpid();
		trace_me_and_stop();
		switch (arg_index) {
		case 0: syscall(PINK_SYSCALL_INVALID, expstr, 0, 0, 0, -1, 0); break;
		case 1: syscall(PINK_SYSCALL_INVALID, 0, expstr, 0, 0, -1, 0); break;
		case 2: syscall(PINK_SYSCALL_INVALID, 0, 0, expstr, 0, -1, 0); break;
		case 3: syscall(PINK_SYSCALL_INVALID, 0, 0, 0, expstr, -1, 0); break;
		case 4: syscall(PINK_SYSCALL_INVALID, 0, 0, 0, 0, expstr, 0);  break;
		case 5: syscall(PINK_SYSCALL_INVALID, 0, 0, 0, 0, -1, expstr); break;
		default: _exit(1);
		}
		_exit(1); /* expect to be killed */
	}
	process_alloc_or_kill(pid, &current);

	LOOP_WHILE_TRUE() {
		int status;
		pid_t tracee_pid;
		long argval, sysnum;

		tracee_pid = wait_verbose(&status);
		if (tracee_pid <= 0 && check_echild_or_kill(pid, tracee_pid))
			break;
		if (check_exit_code_or_fail(status, 0))
			break;
		check_signal_or_fail(status, 0);
		check_stopped_or_kill(tracee_pid, status);
		if (WSTOPSIG(status) == SIGSTOP) {
			trace_setup_or_kill(pid, test_options);
		} else if (WSTOPSIG(status) == (SIGTRAP|0x80)) {
			process_update_regset_or_kill(current);
			read_syscall_or_kill(current, &sysnum);
			check_syscall_equal_or_kill(pid, sysnum, PINK_SYSCALL_INVALID);
			read_argument_or_kill(current, arg_index, &argval);
			read_vm_data_or_kill(current, argval, newstr, sizeof(expstr));
			check_memory_equal_or_kill(pid, newstr, expstr, sizeof(expstr));
			it_worked = true;
			kill(pid, SIGKILL);
			break;
		}
		trace_syscall_or_kill(pid, 0);
	}

	if (!it_worked)
		fail_verbose("Test for reading VM data at argument %d failed", arg_index);
}
END_TEST

/*
 * Test whether reading tracee's address space works.
 * First fork a new child, call syscall(PINK_SYSCALL_INVALID, ...) with a
 * string containing '\0' in the middle and then check whether it's read
 * correctly.
 */
START_TEST(TEST_read_vm_data_nul)
{
	pid_t pid;
	struct pink_process *current;
	bool it_worked = false;
	int arg_index = _i;
	char expstr[] = "trace\0pink"; /* Pink hiding behind the wall again... */
	char newstr[sizeof(expstr)];
#define EXPSTR_LEN 6

	pid = fork_assert();
	if (pid == 0) {
		pid = getpid();
		trace_me_and_stop();
		switch (arg_index) {
		case 0: syscall(PINK_SYSCALL_INVALID, expstr, 0, 0, 0, -1, 0); break;
		case 1: syscall(PINK_SYSCALL_INVALID, 0, expstr, 0, 0, -1, 0); break;
		case 2: syscall(PINK_SYSCALL_INVALID, 0, 0, expstr, 0, -1, 0); break;
		case 3: syscall(PINK_SYSCALL_INVALID, 0, 0, 0, expstr, -1, 0); break;
		case 4: syscall(PINK_SYSCALL_INVALID, 0, 0, 0, 0, expstr, 0);  break;
		case 5: syscall(PINK_SYSCALL_INVALID, 0, 0, 0, 0, -1, expstr); break;
		default: _exit(1);
		}
		_exit(0);
	}
	process_alloc_or_kill(pid, &current);

	LOOP_WHILE_TRUE() {
		int status;
		pid_t tracee_pid;
		long argval, sysnum;

		tracee_pid = wait_verbose(&status);
		if (tracee_pid <= 0 && check_echild_or_kill(pid, tracee_pid))
			break;
		if (check_exit_code_or_fail(status, 0))
			break;
		check_signal_or_fail(status, 0);
		check_stopped_or_kill(tracee_pid, status);
		if (WSTOPSIG(status) == SIGSTOP) {
			trace_setup_or_kill(pid, test_options);
		} else if (WSTOPSIG(status) == (SIGTRAP|0x80)) {
			process_update_regset_or_kill(current);
			read_syscall_or_kill(current, &sysnum);
			check_syscall_equal_or_kill(pid, sysnum, PINK_SYSCALL_INVALID);
			read_argument_or_kill(current, arg_index, &argval);
			read_vm_data_nul_or_kill(current, argval, newstr, sizeof(expstr));
			check_string_equal_or_kill(pid, newstr, expstr, EXPSTR_LEN);
			it_worked = true;
			kill(pid, SIGKILL);
			break;
		}
		trace_syscall_or_kill(pid, 0);
	}

	if (!it_worked)
		fail_verbose("Test for reading"
				" nul-terminated VM data"
				" at argument %d failed",
				arg_index);
}
END_TEST

/*
 * Test whether reading NULL-terminated string arrays work.
 * First fork a new child, call syscall(PINK_SYSCALL_INVALID, ...) with a
 * NULL-terminated array and then check whether it's read correctly.
 */
START_TEST(TEST_read_string_array)
{
	pid_t pid;
	struct pink_process *current;
	bool it_worked = false;
	int arg_index = _i;
#undef EXPSTR_LEN
#undef EXPARR_SIZ
#undef EXPSTR_SIZ
#define EXPSTR_LEN 5
#define EXPARR_SIZ 2
#define EXPSTR_SIZ 12
	char *exparr[EXPARR_SIZ] = { "trace\0pink", NULL };
	char newarr[EXPARR_SIZ][EXPSTR_SIZ];

	pid = fork_assert();
	if (pid == 0) {
		pid = getpid();
		trace_me_and_stop();
		switch (arg_index) {
		case 0: syscall(PINK_SYSCALL_INVALID, exparr, 0, 0, 0, -1, 0); break;
		case 1: syscall(PINK_SYSCALL_INVALID, 0, exparr, 0, 0, -1, 0); break;
		case 2: syscall(PINK_SYSCALL_INVALID, 0, 0, exparr, 0, -1, 0); break;
		case 3: syscall(PINK_SYSCALL_INVALID, 0, 0, 0, exparr, -1, 0); break;
		case 4: syscall(PINK_SYSCALL_INVALID, 0, 0, 0, 0, exparr, 0);  break;
		case 5: syscall(PINK_SYSCALL_INVALID, 0, 0, 0, 0, -1, exparr); break;
		default: _exit(1);
		}
		_exit(0);
	}
	process_alloc_or_kill(pid, &current);

	LOOP_WHILE_TRUE() {
		int i, status;
		pid_t tracee_pid;
		long argval, sysnum;
		bool nullptr;

		tracee_pid = wait_verbose(&status);
		if (tracee_pid <= 0 && check_echild_or_kill(pid, tracee_pid))
			break;
		if (check_exit_code_or_fail(status, 0))
			break;
		check_signal_or_fail(status, 0);
		check_stopped_or_kill(tracee_pid, status);
		if (WSTOPSIG(status) == SIGSTOP) {
			trace_setup_or_kill(pid, test_options);
		} else if (WSTOPSIG(status) == (SIGTRAP|0x80)) {
			process_update_regset_or_kill(current);
			read_syscall_or_kill(current, &sysnum);
			check_syscall_equal_or_kill(pid, sysnum, PINK_SYSCALL_INVALID);
			read_argument_or_kill(current, arg_index, &argval);
			for (i = 0; i < EXPARR_SIZ; i++) {
				info("\tChecking array index %d\n", i);
				read_string_array_or_kill(current,
							  argval, i,
							  newarr[i], sizeof(newarr[i]),
							  &nullptr);
				if (nullptr) {
					if (i + 1 == EXPARR_SIZ)
						break;
					kill(pid, SIGKILL);
					fail_verbose("unexpected NULL pointer"
							" at index %d"
							" (expected:%d)",
							i, EXPARR_SIZ - 1);
				}
				check_string_equal_or_kill(pid, newarr[i], exparr[i], EXPSTR_LEN);
			}
			it_worked = true;
			kill(pid, SIGKILL);
			break;
		}
		trace_syscall_or_kill(pid, 0);
	}

	if (!it_worked)
		fail_verbose("Test for reading NULL-terminated string array"
			     " at argument %d failed", arg_index);
}
END_TEST

TCase *create_testcase_read(void)
{
	TCase *tc = tcase_create("read");

	tcase_add_test(tc, TEST_read_syscall);
	tcase_add_test(tc, TEST_read_retval_good);
	tcase_add_test(tc, TEST_read_retval_fail);
	tcase_add_loop_test(tc, TEST_read_argument, 0, PINK_MAX_ARGS);
	tcase_add_loop_test(tc, TEST_read_vm_data, 0, PINK_MAX_ARGS);
	tcase_add_loop_test(tc, TEST_read_vm_data_nul, 0, PINK_MAX_ARGS);
	tcase_add_loop_test(tc, TEST_read_string_array, 0, PINK_MAX_ARGS);

	return tc;
}
