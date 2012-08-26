/*
Copyright (c) 2012 Alexandre Girao <alexgirao@gmail.com>.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
1. Redistributions of source code must retain the above copyright notice(s),
   this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice(s),
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER(S) ``AS IS'' AND ANY EXPRESS
OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO
EVENT SHALL THE COPYRIGHT HOLDER(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * Character Generator Protocol (CHARGEN)
 *
 * reference: http://en.wikipedia.org/wiki/Character_Generator_Protocol
 * reference: http://tools.ietf.org/html/rfc864
 *
 * usage example:
 *

./chargenx -ia -n10 -b0 -o out -t
./chargenx -ib -n10 -b0 -o out -c $((33+10-1)) -r
./chargenx -b1000000 -ochargenx.out -t -pchargenx.pid
./chargenx -n30 -b500000 -pchargenx.pid -d -ochargenx.out -echargenx.err

(
  set -eux
  ./chargenx -n5 -pchargenx.pid -d -ochargenx.out -echargenx.err -a2000000
  sleep 1
  lsof -p"`cat chargenx.pid`"
  tail -fn0 chargenx.out
)

(
  set -eux
  rm -fv chargenx.out-* chargenx.out
  ./chargenx -n100 -b0 | sha1sum
  ./chargenx -n100 -b100000 -d -pchargenx.pid -ochargenx.out -t -echargenx.err
  sleep 3 && mv chargenx.out chargenx.out-"`date +%s`" && kill -USR1 "`cat chargenx.pid`"
  sleep 2 && mv chargenx.out chargenx.out-"`date +%s`" && kill -USR1 "`cat chargenx.pid`"
  sleep 1 && mv chargenx.out chargenx.out-"`date +%s`" && kill -USR1 "`cat chargenx.pid`"
  sleep 5
  cat chargenx.out-* chargenx.out | sha1sum
  wc -l chargenx.out-* chargenx.out
)

 *
 */

/*
 * reference: https://www.securecoding.cert.org/confluence/display/seccode/SIG31-C.+Do+not+access+or+modify+shared+objects+in+signal+handlers
 * reference: http://www.enderunix.org/docs/eng/daemon.php
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>

#include "debug0.h"

#include "bsd-getopt_long.h"
#include "getopt_x.h"

#define START_CHAR 33
#define END_CHAR 127  /* exclusive */
#define LINE_LENGTH 72

volatile sig_atomic_t got_SIGUSR1 = 0;
volatile sig_atomic_t got_SIGHUP = 0;
volatile sig_atomic_t got_SIGTERM = 0;

static int write_exact(int fd, void *buf, int len)
{
	int i, wrote = 0;
	do {
		if ((i = write(fd, buf + wrote, len - wrote)) <= 0) return i;
		wrote += i;
	} while (wrote < len);
	return len;
}

/* getopt_x
 * reference: test-getopt-5.c
 */

static const char *options_short = NULL;
static const char *options_mandatory = NULL;

static struct option options_long[] = {
	{.val='a', .name="start-delay", .has_arg=1},
	{.val='z', .name="end-delay", .has_arg=1},
	{.val='b', .name="between-delay", .has_arg=1},
	{.val='u', .name="between-lines", .has_arg=1},
	{.val='n', .name="lines", .has_arg=1},
	{.val='i', .name="id", .has_arg=1},
	{.val='p', .name="pid-file", .has_arg=1},
	{.val='c', .name="start-char", .has_arg=1},
	{.val='r', .name="reverse-order"},
	{.val='o', .name="stdout-file", .has_arg=1},
	{.val='e', .name="stderr-file", .has_arg=1},
	{.val='t', .name="truncate-file"},
	{.val='d', .name="daemonize"},
	{.val='h', .name="help"},
	{.name=NULL}
};

struct args {
	char pid_file[256];
	long start_delay;
	long between_delay;
	long between_lines;
	long end_delay;
	long lines;
	char id[100];
	int start_char;
	int reverse_order;
	char out_file[256];
	char err_file[256];
	int truncate;
	int daemonize;
} args[1] = {
	{
		.between_delay = 100 * 1000, /* 100 milliseconds */
		.lines = -1L,
		.start_char = START_CHAR
	}
};

/* sub or zero */
#define SOZ(a,b) ((a) > (b) ? (a) - (b) : 0)

static void help(const char *argv0, struct getopt_x *state)
{
	char buf[4096];
	int bufsz = sizeof(buf);
	struct option opt[1];
	int pos = 0;
	int c = 0;

	pos += snprintf(buf + pos, SOZ(bufsz,pos), "\n");
	pos += snprintf(buf + pos, SOZ(bufsz,pos), "  usage: %s [options] ...\n", argv0);
	pos += snprintf(buf + pos, SOZ(bufsz,pos), "  options:\n");
	pos += snprintf(buf + pos, SOZ(bufsz,pos), "\n");

	while ((c = getopt_x_option(state, c, opt)) >= 0) {
		pos += getopt_x_option_format(buf + pos, bufsz - pos, state, opt);
		switch (opt->val) {
		case 'a':
			pos += snprintf(buf + pos, SOZ(bufsz,pos), "start delay in usecs, default is without delay\n");
			break;
		case 'z':
			pos += snprintf(buf + pos, SOZ(bufsz,pos), "end delay in usecs, default is withour delay\n");
			break;
		case 'b':
			pos += snprintf(buf + pos, SOZ(bufsz,pos), "between line delay in usecs, default\n");
			pos += getopt_x_option_format(buf + pos, SOZ(bufsz,pos), state, NULL);
			pos += snprintf(buf + pos, SOZ(bufsz,pos), "is %li usecs\n", args->between_delay);
			break;
		case 'u':
			pos += snprintf(buf + pos, SOZ(bufsz,pos), "a microsecond sleep after each # lines\n");
			pos += getopt_x_option_format(buf + pos, SOZ(bufsz,pos), state, NULL);
			pos += snprintf(buf + pos, SOZ(bufsz,pos), "this conflicts with -b\n");
			break;
		case 'n':
			pos += snprintf(buf + pos, SOZ(bufsz,pos), "number of lines (default to unlimited), %i\n", END_CHAR - START_CHAR);
			pos += getopt_x_option_format(buf + pos, SOZ(bufsz,pos), state, NULL);
			pos += snprintf(buf + pos, SOZ(bufsz,pos), "loop on all chars\n");
			break;
		case 'i':
			pos += snprintf(buf + pos, SOZ(bufsz,pos), "line identification prefix\n");
			break;
		case 'p':
			pos += snprintf(buf + pos, SOZ(bufsz,pos), "file to write the process pid, default\n");
			pos += getopt_x_option_format(buf + pos, SOZ(bufsz,pos), state, NULL);
			pos += snprintf(buf + pos, SOZ(bufsz,pos), "is to suppress pid information\n");
			break;
		case 'c':
			pos += snprintf(buf + pos, SOZ(bufsz,pos), "from %i ('%c') to %i ('%c')\n",
					START_CHAR, START_CHAR, END_CHAR - 1, END_CHAR - 1);
			break;
		case 'o':
			pos += snprintf(buf + pos, SOZ(bufsz,pos), "path to redirect stdout, re-open file upon SIGUSR1\n");
			break;
		case 'e':
			pos += snprintf(buf + pos, SOZ(bufsz,pos), "path to redirect stderr\n");
			break;
		case 't':
			pos += snprintf(buf + pos, SOZ(bufsz,pos), "do not append to output file, but truncate\n");
			break;
		case 'd':
			pos += snprintf(buf + pos, SOZ(bufsz,pos), "run on background\n");
			break;
		case 'r':
		case 'h':
			pos += snprintf(buf + pos, SOZ(bufsz,pos), "\n");
			break;
		default:
			pos += snprintf(buf + pos, SOZ(bufsz,pos), "undocumented\n");
		}
		if (pos >= bufsz) {
			DEBUG("buffer too small");
			exit(1);
		}
	}
	pos += snprintf(buf + pos, SOZ(bufsz,pos), "\n");

	fputs(buf, stderr);
}

static int process_args(struct getopt_x *state, int argc, char **argv)
{
	int c;
	if (getopt_x_prepare(state, argc, argv, options_short, options_long, options_mandatory)) {
		DEBUG("error: failed to parse options");
		exit(1);
	}
	do {
		struct option *opt;
		switch (c = getopt_x_next(state, &opt)) {
		case 'a': args->start_delay = atol(optarg); break;
		case 'z': args->end_delay = atol(optarg); break;
		case 'b': args->between_delay = atol(optarg); break;
		case 'u': args->between_lines = atol(optarg); break;
		case 'n': args->lines = atol(optarg); break;
		case 'i': strncpy_sizeof(args->id, optarg); break;
		case 'p': strncpy_sizeof(args->pid_file, optarg); break;
		case 'c': args->start_char = atoi(optarg); break;
		case 'r': args->reverse_order = 1; break;
		case 'o': strncpy_sizeof(args->out_file, optarg); break;
		case 'e': strncpy_sizeof(args->err_file, optarg); break;
		case 't': args->truncate = 1; break;
		case 'd': args->daemonize = 1; break;
		case 'h': help(argv[0], state); exit(0);
		case -1: break;
		default:
			getopt_x_option_debug(state, c, opt);
			return -1;
		}
	} while (c != -1);

	assert(args->start_delay >= 0);
	assert(args->between_delay >= 0);
	assert(args->end_delay >= 0);

	assert(args->start_char >= START_CHAR);
	assert(args->start_char < END_CHAR);

	if (args->truncate && *args->out_file == 0) {
		DEBUG("warning: -t specified without -o");
	}

	if (args->daemonize) {
		if (*args->out_file == 0) {
			DEBUG("warning: redirecting stdout to /dev/null");
			strncpy_sizeof(args->out_file, "/dev/null");
		}
		if (*args->err_file == 0) {
			DEBUG("warning: redirecting stderr to /dev/null");
			strncpy_sizeof(args->err_file, "/dev/null");
		}
	}

	if (GETOPT_X_OPTION_IS_INFORMED(state, 'u')) {
		if (GETOPT_X_OPTION_IS_INFORMED(state, 'b')) {
			DEBUG("error: -b option conflicts with -u option");
			return -1;
		}
		assert(args->between_lines > 0);
	}

	return state->got_error;
}

static int doit();
static int write_pid(const char *pid_file);
static void signal_SIGUSR1(int sig);
static void signal_SIGHUP(int sig);
static void signal_SIGTERM(int sig);
static int daemonize(int close_all_descriptors, int preserve_stderr);
static int open_stdout();
static int open_stderr();

int main(int argc, char **argv)
{
	int pid_fd = -1;
	struct getopt_x state[1];

	if (process_args(state, argc, argv)) {
		help(argv[0], state);
		exit(1);
	}

	if (args->daemonize) {
		if (daemonize(1 /* close_all_descriptors */, 1 /* preserve_stderr */)) return -1;
		signal(SIGUSR1, signal_SIGUSR1);
	} else {
		signal(SIGHUP, signal_SIGHUP);
		signal(SIGTERM, signal_SIGTERM);
		signal(SIGUSR1, signal_SIGUSR1);
	}

	/* after daemonize, both stdin and stdout points to /dev/null
	 */

	if (*args->err_file) if (open_stderr()) return -1;
	if (*args->out_file) {
		if (open_stdout()) return -1;
		DEBUG("stdout successfully redirected to [%s]", args->out_file);
	}

	if (*args->pid_file && (pid_fd = write_pid(args->pid_file)) < 0) {
		DEBUG("error: failed to write pid or acquire lock");
		return -1;
	}

	DEBUG("program [%s] started", argv[0]);

	if (args->start_delay) usleep(args->start_delay);

	DEBUG("program [%s] after start delay", argv[0]);

	if (doit()) return 1;

	DEBUG("program [%s] before end delay", argv[0]);

	if (args->end_delay) usleep(args->end_delay);

	DEBUG("program [%s] finished", argv[0]);

	if (*args->out_file) close(STDOUT_FILENO);
	if (*args->err_file) close(STDERR_FILENO);

	if (pid_fd != -1) close(pid_fd);

	return 0;
}

static int reopen_stdout();

static int doit()
{
	char doubleline[(END_CHAR - START_CHAR) * 2 + 1];
	int i;
	char *buf = NULL;
	int bufsz = 0;
	char *line = NULL;

	for (i=0; i<(END_CHAR - START_CHAR) * 2; i++) {
		doubleline[i] = (i % (END_CHAR - START_CHAR)) + START_CHAR;
	}
	doubleline[i] = '\0';

	if (*args->id) {
		int id_len = strlen(args->id);
		bufsz = id_len + 1 + LINE_LENGTH + 1;
		buf = malloc(bufsz);
		assert(buf);
		strcpy(buf, args->id);
		buf[id_len] = ' ';
		line = buf + id_len + 1;
	} else {
		bufsz = LINE_LENGTH + 1;
		buf = malloc(bufsz);
		assert(buf);
		line = buf;
	}

	if (args->lines) {
		int fd = STDOUT_FILENO;
		long count = 1;
		long n = args->lines;
		long b = args->between_delay;
		long u = args->between_lines;
		int skip = args->reverse_order ? END_CHAR - START_CHAR - 1 : 1;

		DEBUG("before main loop");

		i = args->start_char - START_CHAR;
		for (;;) {
			if (got_SIGUSR1) {
				got_SIGUSR1 = 0;
				DEBUG("got SIGUSR1");
				if (*args->out_file) {
					DEBUG("re-opening [%s]", args->out_file);
					if (reopen_stdout()) return -1;
					DEBUG("stdout successfully redirected to [%s]", args->out_file);
				}
			}

			memcpy(line, doubleline + i, LINE_LENGTH);
			line[LINE_LENGTH] = '\n';

			assert(write_exact(fd, buf, bufsz) == bufsz);

			i = (i + skip) % (END_CHAR - START_CHAR);
			if (!--n) break;

			if (u) {
				if (count % u == 0) usleep(1);
			} else if (b) usleep(b);

			if (got_SIGTERM) {
				got_SIGTERM = 0;
				DEBUG("got SIGTERM");
				break;
			}

			if (got_SIGHUP) {
				got_SIGHUP = 0;
				DEBUG("got SIGHUP");
				i = 0;
			}

			count++;
		}

		DEBUG("after main loop");
	}

	/* cleanup
	 */

	free(buf);
	buf = NULL;
	line = NULL;

	return 0;
}

/* return fd
 */
static int write_pid(const char *pid_file)
{
	int i;
	int fd;
	char buf[100];
	if ((fd = open(pid_file, O_WRONLY | O_CREAT, 0644)) < 0) {
		int save_errno = errno;
		assert(fd == -1);
		DEBUG("open(pid_file=[%s], O_WRONLY | O_CREAT, 0644), errno=%i", pid_file, save_errno);
		errno = save_errno;
		perror(pid_file);
		return -1;
	}
	if ((i = lockf(fd, F_TLOCK, 0)) < 0) {
		int save_errno = errno;
		assert(i == -1);
		DEBUG("lockf(fd, F_TLOCK, 0), errno=%i", save_errno);
		errno = save_errno;
		perror(pid_file);
		return -1;
	}
	i = snprintf(buf, sizeof(buf), "%i\n", getpid());
	assert(write_exact(fd, buf, i) == i);
	assert(ftruncate(fd, i) == 0);
	return fd;
}

static void signal_SIGUSR1(int sig)
{
	got_SIGUSR1 = 1;
}

static void signal_SIGHUP(int sig)
{
	got_SIGHUP = 1;
}

static void signal_SIGTERM(int sig)
{
	got_SIGTERM = 1;
}

static int open_stdout0(int flags)
{
	int fd;
	assert(*args->out_file);
	if ((fd = open(args->out_file, flags, 0644)) < 0) {
		int save_errno = errno;
		assert(fd == -1);
		DEBUG("open(args->out_file=[%s], flags=%i, 0644), errno=%i", args->out_file, flags, save_errno);
		errno = save_errno;
		perror(args->out_file);
		return -1;
	}
	if (fd != STDOUT_FILENO) {
		assert(dup2(fd, STDOUT_FILENO) == STDOUT_FILENO);
		assert(close(fd) == 0);
	}
	return 0;
}

static int open_stderr0(int flags)
{
	int fd;
	assert(*args->err_file);
	if ((fd = open(args->err_file, flags, 0644)) < 0) {
		int save_errno = errno;
		assert(fd == -1);
		DEBUG("open(args->err_file=[%s], flags=%i, 0644), errno=%i", args->err_file, flags, save_errno);
		errno = save_errno;
		perror(args->err_file);
		return -1;
	}
	if (fd != STDERR_FILENO) {
		assert(dup2(fd, STDERR_FILENO) == STDERR_FILENO);
		assert(close(fd) == 0);
	}
	return 0;
}

static int open_stdout()
{
	if (args->truncate) return open_stdout0(O_CREAT | O_WRONLY | O_TRUNC);
	return open_stdout0(O_CREAT | O_WRONLY | O_APPEND);
}

static int reopen_stdout()
{
	return open_stdout0(O_CREAT | O_WRONLY | O_APPEND);
}

static int open_stderr()
{
	return open_stderr0(O_CREAT | O_WRONLY | O_APPEND);
}

static int daemonize(int close_all_descriptors, int preserve_stderr)
{
	int pid;

	if ((pid = fork()) == 0) {
		/* child
		 */

		int fd, i;

		/* obtain a new process group
		 */
		setsid();

		if (close_all_descriptors) {
			for (i = getdtablesize(); i >= 0; i--) {
				if (preserve_stderr && i == STDERR_FILENO) continue;
				close(i);
			}
		}

		/* redirect descriptors to /dev/null
		 */

		if ((fd = open("/dev/null", O_RDWR)) < 0) {
			int save_errno = errno;
			assert(fd == -1);
			DEBUG("open(\"/dev/null\", O_RDWR), errno=%i", save_errno);
			errno = save_errno;
			perror("/dev/null");
			return -1;
		}

		assert(dup2(fd, STDIN_FILENO) == STDIN_FILENO);
		assert(dup2(fd, STDOUT_FILENO) == STDOUT_FILENO);
		if (preserve_stderr == 0) {
			assert(dup2(fd, STDERR_FILENO) == STDERR_FILENO);
		}
		if (fd != STDIN_FILENO && fd != STDOUT_FILENO && fd != STDERR_FILENO) {
			assert(close(fd) == 0);
			fd = -1;
		}

		umask(027);

		/* first instance continues
		 */
		signal(SIGCHLD, SIG_IGN); /* ignore child */
		signal(SIGTSTP, SIG_IGN); /* ignore tty signals */
		signal(SIGTTOU, SIG_IGN);
		signal(SIGTTIN, SIG_IGN);
		signal(SIGHUP, signal_SIGHUP); /* catch hangup signal */
		signal(SIGTERM, signal_SIGTERM); /* catch kill signal */

		return 0;
	} else if (pid < 0) {
		perror("fork()");
		return -1;
	}

	/* parent exits
	 */
	exit(0);

	return 0;
}
