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

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdarg.h>
#include <time.h>

//#define NO_DEBUG
#include "debug0.h"
#include "dict.h"

#include "subprocess.h"

//#define DEBUG_INFO_ENABLED

#ifndef DEBUG_INFO_ENABLED
#  if defined (__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#    define DEBUG_INFO(...)	    //
#  elif defined (__GNUC__)
#    define DEBUG_INFO(format...)   //
#  endif
#else
#  define DEBUG_INFO DEBUG
#endif

#define USE_CLOCK_GETTIME
#define MAX_WAIT_SUBPROCESS_EINTR_COUNT 25

#define DELTA_USEC(a,b) (((a)->tv_sec - (b)->tv_sec) * 1000000 + (a)->tv_usec - (b)->tv_usec)
#define DELTA_MSEC(a,b) (DELTA_USEC(a,b) / 1000)

int make_fd_non_blocking(int fd)
{
	int flags, s;

	if ((flags = fcntl(fd, F_GETFL, 0)) == -1) {
		perror("fcntl(fd, F_GETFL, 0)");
		return __LINE__;
	}

	flags |= O_NONBLOCK;
	if ((s = fcntl(fd, F_SETFL, flags)) == -1) {
		perror("fcntl(fd, F_SETFL, flags)");
		return __LINE__;
	}

	return 0;
}

#if 1
static int xpipe(int pipefd[2])
{
	int r;
	r = pipe(pipefd);
	DEBUG_INFO("pipe(pipefd)=%i, pipefd = {%i, %i}", r, pipefd[0], pipefd[1]);
	return r;
}
static int xclose(int fd)
{
	int r;
	r = close(fd);
	DEBUG_INFO("close(%i)=%i", fd, r);
	return r;
}
#else
#define xpipe pipe
#define xclose close
#endif

/* children/child dictionary
 */

DEFINE_DICT(children, child,
	    struct subprocess *sp;
  );

struct child *child_new(struct subprocess *sp)
{
	struct child *c = child_new0();
	assert(c);
	c->sp = sp;
	return c;
}

void child_free(struct child * x)
{
	if (x) {
		x->sp->is_gone = 1;
		child_free0(x);
	}
}

int child_cmp(struct child *a, struct child *b)
{
	return a->sp->pid - b->sp->pid;
}

int child_cmp_by_pid(int a, struct child *b)
{
	return a - b->sp->pid;
}

struct child *children_search_by_pid(struct children * rbtree, int key)
{
	struct child *ret;
	int cmp;
	ret = rbtree->rbt_root;
	while (ret != &rbtree->rbt_nil && (cmp = child_cmp_by_pid(key, ret)) != 0) {
		if (cmp < 0) {
			ret = rbtn_left_get(struct child, _meta, ret);
		} else {
			ret = rbtn_right_get(struct child, _meta, ret);
		}
	}
	if (ret == &rbtree->rbt_nil) {
		ret = NULL;
	}
	return ret;
}

void children_free(struct children * x)
{
    if (x) {
	struct child *c, *n;
	for (c = children_first(x); c; c = n) {
	    n = children_next(x, c);
	    children_remove(x, c);
	    child_free(c);
	}
	free(x);
    }
}

/*
 */

void get_current_timeval(struct timeval *tv)
{
#ifdef USE_CLOCK_GETTIME
	struct timespec ts[1];
	assert(clock_gettime(CLOCK_REALTIME, ts) == 0);
	tv->tv_sec = ts->tv_sec;
	tv->tv_usec = ts->tv_nsec / 1000;
#else
	assert(gettimeofday(tv, NULL) == 0);
#endif
}

static int write_exact(int fd, void *buf, int len)
{
	int i, wrote = 0;
	do {
		if ((i = write(fd, buf + wrote, len - wrote)) <= 0) {
			return i;
		}
		wrote += i;
	} while (wrote < len);
	return len;
}

#define MAX2(a, b) ((a) >= (b) ? (a) : (b))
#define MAX3(a, b, c) MAX2(MAX2(a, b), c)

static int selfpipe[2] = {-1, -1};
static struct children *subprocesses = NULL;
static volatile sig_atomic_t got_SIGCHLD = 0;
static sigset_t sigset_SIGCHLD[1];

int subprocess_get_selfpipe_read_fd()
{
	return selfpipe[0];
}

#define DEC_DIGIT(v) ((v) >= 0 && (v) < 10 ? "0123456789"[v] : '?')

/* will fill 11 octets starting from s-1 (strrchr() compatible) and
 * going backwards
 */
static void backfill_u4_dec(char *s, int v, int pad)
{
	char *r = s - 11;
	assert(s);
	if (v > 0) {
		while (v) {
			*--s = DEC_DIGIT(v % 10);
			v = v / 10;
		}
	} else if (v < 0) {
		unsigned int x = -v;
		while (x) {
			*--s = DEC_DIGIT(x % 10);
			x = x / 10;
		}
		*--s = '-';
	} else {
		*--s = '0';
	}
	while (r < s) {
		*--s = pad;
	}
}

/*
 * please read `man 7 signal' in `Async-signal-safe functions' prior
 * to changing the handler below
 *
 * in stress tests, we were able to write 16384 signals before
 * blocking, a clear indication that a pipe (in linux 2.6) has a 65536
 * byte buffer, cool, lots of data
 *
 * reference: http://www.cs.utah.edu/dept/old/texinfo/glibc-manual-0.02/library_21.html
 *
 */
static void sigaction_SIGCHLD(int n, siginfo_t *si, void *vp)
{
	char msg[] = "sigaction_SIGCHLD(), pid_gone=-1234567890\n";

	//assert(sizeof(si->si_pid) == sizeof(int));

	backfill_u4_dec(msg + sizeof(msg) - 2 /* "\n\0" */, si->si_pid, ' ');
	write_exact(STDERR_FILENO, msg, sizeof(msg) - 1);

	if (selfpipe[1] != -1) {
		int r = write(selfpipe[1], (void*)&si->si_pid, sizeof(si->si_pid));
		if (r == -1 && errno == EAGAIN) {
			char msg[] = "sigaction_SIGCHLD(), pipe buffer is full, drained -1234567890 bytes\n";
			char buf[4096];
			r = read(selfpipe[0], buf, sizeof(buf));
			backfill_u4_dec(msg + sizeof(msg) - 8 /* " bytes\n\0" */, r, ' ');
			write_exact(STDERR_FILENO, msg, sizeof(msg) - 1);
		}
	}

	got_SIGCHLD = 1;
}

#if 0
/* returns 0 if signal handler was reseted
 */
static int reset_signal_handlers()
{
	struct sigaction act[1];
	memset(act, 0, sizeof(act));
	sigaction(SIGCHLD, NULL, act);
	if (act->sa_sigaction || act->sa_handler) {
		memset(act, 0, sizeof(act));
		sigaction(SIGCHLD, act, NULL);

		assert(selfpipe[0] != -1);
		assert(selfpipe[1] != -1);

		assert(xclose(selfpipe[1]) == 0);
		selfpipe[1] = -1;

		assert(xclose(selfpipe[0]) == 0);
		selfpipe[0] = -1;

		return 0;
	}
	return 1;
}
#endif

static int selfpipe_setup()
{
	int r;
	static struct sigaction act;
	static struct sigaction pip;

	if (selfpipe[0] != -1) {
		/* already initialized
		 */
		DEBUG_INFO("selfpipe already setup");
		return 0;
	}

	/* check if there is a signal already installed
	 */

	{
		struct sigaction old;
		memset(&old, 0, sizeof(old));
		sigaction(SIGCHLD, NULL, &old);
		assert(old.sa_sigaction == NULL);
	}

	/* setup
	 */

	if ((r = xpipe(selfpipe))) {
		int save_errno = errno;
		assert(r == -1);
		DEBUG("pipe(selfpipe), errno=%i", save_errno);
		errno = save_errno;
		perror("pipe(selfpipe)");
		abort();
	}

	assert(make_fd_non_blocking(selfpipe[0]) == 0);
	assert(make_fd_non_blocking(selfpipe[1]) == 0);

	/* SIGCHLD signal handling
	 */

	memset(&act, 0, sizeof(act));
	sigemptyset(&act.sa_mask);
	act.sa_sigaction = sigaction_SIGCHLD;
	act.sa_flags = SA_SIGINFO;
	assert(sigaction(SIGCHLD, &act, NULL) == 0);

	/* SIGPIPE signal handling
	 */

	memset(&pip, 0, sizeof(pip));
	sigemptyset(&pip.sa_mask);
	pip.sa_handler = SIG_IGN;
	assert(sigaction(SIGPIPE, &pip, NULL) == 0);

	/* sigset_SIGCHLD
	 */

	assert(sigemptyset(sigset_SIGCHLD) == 0);
	assert(sigaddset(sigset_SIGCHLD, SIGCHLD) == 0);

	/*
	 */

	subprocesses = children_new0();
	assert(subprocesses);

	return 0;
}

void interrupt_safe_sleep(int msec)
{
	int r;
	struct timeval start;
	struct timeval current;
	struct timeval tv;
	long usec;
	long dmsec;

	get_current_timeval(&start);

	DEBUG_INFO("interrupt_safe_sleep(%u)", msec);

	tv.tv_sec = 0;
	tv.tv_usec = msec * 1000;
	r = select(0, NULL, NULL, NULL, &tv);
	while (r < 0) {
		assert(r == -1);
		assert(errno == EINTR);

		get_current_timeval(&current);
		if ((dmsec = DELTA_MSEC(&current, &start)) >= msec) {
			break;
		}

		DEBUG_INFO("resuming interrupt safe sleep, %li milliseconds to go, total %i", msec - dmsec, msec);
		tv.tv_sec = 0;
		tv.tv_usec = (msec - dmsec) * 1000;
		r = select(0, NULL, NULL, NULL, &tv);
	}

	get_current_timeval(&current);
	usec = DELTA_USEC(&current, &start);
	DEBUG_INFO("%li milliseconds elapsed (%lu usecs)", usec / 1000, usec);
}

void subprocess_close_child_fdin(struct subprocess *sp)
{
	assert(sp->pid > 0);
	if (sp->child_fdin >= 0) {
		assert(xclose(sp->child_fdin) == 0);
		sp->child_fdin = -1;
	}
}

void subprocess_close_child_fdout(struct subprocess *sp)
{
	assert(sp->pid > 0);
	if (sp->child_fdout >= 0) {
		assert(xclose(sp->child_fdout) == 0);
		sp->child_fdout = -1;
	}
}

void subprocess_close_child_fderr(struct subprocess *sp)
{
	assert(sp->pid > 0);
	if (sp->child_fderr >= 0) {
		assert(xclose(sp->child_fderr) == 0);
		sp->child_fderr = -1;
	}
}

/* returns 0 on success
 */
static int subprocess_read_selfpipe0()
{
	DEBUG_INFO("subprocess_read_selfpipe(): begin");
	for (;;) {
		char buf_selfpipe[10 * sizeof(int)]; /* maybe read 10 pids at a time */
		int n = read(selfpipe[0], buf_selfpipe, sizeof(buf_selfpipe));
		DEBUG_INFO("subprocess_read_selfpipe(): read(selfpipe[0], buf_selfpipe, sizeof(buf_selfpipe)): %i", n);
		if (n < 0) {
			int save_errno = errno;
			assert(n == -1);
			if (errno == EINTR) {
				DEBUG_INFO("subprocess_read_selfpipe(): received an EINTR");
				break;
			}
			if (errno == EAGAIN) {
				DEBUG_INFO("subprocess_read_selfpipe(): received an EAGAIN");
				break;
			}
			DEBUG("read(), errno=%i", save_errno);
			errno = save_errno;
			perror("reading selfpipe[0]");
			exit(1);
		}
		assert((n % sizeof(int)) == 0);
		if (n) {
			int *pid_gone = (int*)buf_selfpipe;
			int m = n / sizeof(int);
			do {
				struct child *c;
				if ((c = children_search_by_pid(subprocesses, *pid_gone))) {
					assert(*pid_gone == c->sp->pid);
					DEBUG_INFO("subprocess_read_selfpipe(): \x20\x20pid %i found at child %p", *pid_gone, c);
					children_remove(subprocesses, c);
					child_free(c);
					c = NULL;
				} else {
					DEBUG_INFO("subprocess_read_selfpipe(): \x20\x20pid %i does not concern to us", *pid_gone);
				}
				pid_gone++;
			} while (--m);
			if (n == sizeof(buf_selfpipe)) {
				/* last read filled all the buffer
				 */
				DEBUG_INFO("subprocess_read_selfpipe(): checking again");
				continue;
			}
		}
		break;
	}
	DEBUG_INFO("subprocess_read_selfpipe(): done");
	return 0;
}

/* returns 0 on success
 */
int subprocess_read_selfpipe()
{
	if (got_SIGCHLD) {
		/* this may seem paranoid, but, is certainly the right
		 * thing (sigprocmask) to do
		 */
		sigprocmask(SIG_BLOCK, sigset_SIGCHLD, NULL);
		if (got_SIGCHLD) got_SIGCHLD = 0;
		/* we can clear got_SIGCHLD early, since this is the
		 * only place where it is checked for
		 */
		sigprocmask(SIG_UNBLOCK, sigset_SIGCHLD, NULL);
		return subprocess_read_selfpipe0();
	}
	DEBUG_INFO("subprocess_read_selfpipe(): nothing new");
	return 0;
}

/*
 * returns 0 on timeout or returns the pid of child gone
 *
 */
int subprocess_wait(struct subprocess *sp, int msec)
{
	int r, pid = 0;
	int eintr_count = 0;
	fd_set rfds[1];
	struct timeval start[1];
	struct timeval tv[1];
	long dmsec;

	assert(sp->pid > 0);
	assert(selfpipe[0] != -1);
	assert(sp->waitpid_pid == 0); /* you can't wait on a terminated process */

	if (sp->is_gone) {
		/* child terminated, but still defunct (need a call to subprocess_terminate)
		 */
		return sp->pid;
	}

	get_current_timeval(start);

	DEBUG_INFO("subprocess_wait(pid=%i) for %i milliseconds", sp->pid, msec);

	FD_ZERO(rfds);

	for (;;) {
		FD_SET(selfpipe[0], rfds);

		if (msec) {
			struct timeval current[1];
			get_current_timeval(current);
			if ((dmsec = DELTA_MSEC(current, start)) > msec) {
				DEBUG_INFO("elapsed time");
				break;
			}
			tv->tv_sec = 0;
			tv->tv_usec = (msec - dmsec) * 1000;
		} else {
			dmsec = 0;
			tv->tv_sec = 0;
			tv->tv_usec = 0;
		}

		DEBUG_INFO("select(selfpipe[0]+1) for %li milliseconds", msec - dmsec);
		r = select(selfpipe[0]+1, rfds, NULL, NULL, tv);
		if (r < 0) {
			int save_errno = errno;
			assert(r == -1);
			if (errno == EINTR) {
				eintr_count++;
				DEBUG_INFO("select() received an EINTR, count=%i, retrying", eintr_count);
				if (eintr_count == MAX_WAIT_SUBPROCESS_EINTR_COUNT) {
					DEBUG_INFO("too much EINTR, giving up select");
					break;
				}
				continue;
			}
			DEBUG("select(), errno=%i", save_errno);
			errno = save_errno;
			perror("select()");
			exit(1);
		} else if (r) {
			DEBUG_INFO("select()=%i", r);
			if (FD_ISSET(selfpipe[0], rfds)) {
				eintr_count = 0;
				assert(subprocess_read_selfpipe() == 0);
				if (sp->is_gone) {
					DEBUG_INFO("sp=[pid=%i] is gone", sp->pid);
					pid = sp->pid;
					break;
				}
			}
		} else {
			DEBUG_INFO("select() timeout");
			break;
		}
	}

	{
		struct timeval current[1];
		long usec;
		get_current_timeval(current);
		usec = DELTA_USEC(current, start);
		DEBUG_INFO("subprocess_wait(pid=%i gone? %s), %li milliseconds elapsed (%lu usecs), total %i", sp->pid, pid ? "yes" : "no",
		      usec / 100, usec, msec);
	}

	/* timeout or other child gone
	 */
	return pid;
}

static int loop_child_pipes(struct subprocess *sp, struct subprocess_callbacks *cb)
{
	fd_set rfds[1], wfds[1];
	struct timeval tv;
	int r, max_fds;

	if (cb->ctimeout == 0) {
		cb->ctimeout = 5000; /* 5 seconds */
	}
	if (cb->ptimeout == 0) {
		cb->ptimeout = 5000; /* 5 seconds */
	}

	assert(cb->ctimeout > 0);
	assert(cb->ptimeout > 0);

	FD_ZERO(rfds);
	FD_ZERO(wfds);

	if (cb->produce_immediately) {
		DEBUG_INFO("producing immediately");
		goto BURST;
	}

	for (;;) {
		max_fds = -1;
		if (selfpipe[0] != -1) {
			FD_SET(selfpipe[0], rfds);
			max_fds = MAX2(selfpipe[0], max_fds);
		}
		if (sp->child_fdout != -1) {
			FD_SET(sp->child_fdout, rfds);
			max_fds = MAX2(sp->child_fdout, max_fds);
		}
		if (sp->child_fderr != -1) {
			FD_SET(sp->child_fderr, rfds);
			max_fds = MAX2(sp->child_fderr, max_fds);
		}

		assert(max_fds >= 0);

		tv.tv_sec = 0;
		tv.tv_usec = cb->ctimeout * 1000;

		r = select(max_fds+1, rfds, NULL, NULL, &tv);
		if (r < 0) {
			int save_errno = errno;
			assert(r == -1);
			if (errno == EINTR) {
				DEBUG_INFO("select() received an EINTR, retrying, r=%i, pid=%i", r, sp->pid);
				continue;
			}
			DEBUG("select(), errno=%i, pid=%i", save_errno, sp->pid);
			DEBUG_INFO("sp->child_fdin=%i", sp->child_fdin);
			DEBUG_INFO("sp->child_fdout=%i", sp->child_fdout);
			DEBUG_INFO("sp->child_fderr=%i", sp->child_fderr);
			errno = save_errno;
			perror("select()");
			return __LINE__;
		} else if (r) {
			int n;

			if (sp->child_fdout != -1) {
				/* rfds/sp->child_fdout
				 */

				n = FD_ISSET(sp->child_fdout, rfds);
				if (n) {
					/* read from child's STDOUT
					 */
					char buf_out[0x7fff];
					DEBUG_INFO("FD_ISSET(sp->child_fdout, rfds): %i", n);
					n = read(sp->child_fdout, buf_out, sizeof(buf_out)-1);
					if (n < 0) {
						int save_errno = errno;
						assert(n == -1);
						if (errno == EINTR) {
							DEBUG_INFO("read() received an EINTR, retrying");
							continue;
						}
						DEBUG("read(), errno=%i", save_errno);
						errno = save_errno;
						perror("reading child's stdout");
						exit(1);
					}
					if (n) {
						assert(n < sizeof(buf_out) && n > 0);
						buf_out[n] = 0;
						DEBUG_INFO("received [%s] in child's stdout", buf_out);
						if (cb->consume_stdout) {
							cb->consume_stdout(sp, buf_out, n);
						} else {
							DEBUG_INFO("received data is going nowhere");
						}
					} else {
						DEBUG_INFO("child's fdout got EOF");
						FD_CLR(sp->child_fdout, rfds);
						subprocess_close_child_fdout(sp);
						assert(sp->child_fdout == -1);
					}
				}
			}

			if (sp->child_fderr != -1) {
				/* rfds/sp->child_fderr
				 */

				n = FD_ISSET(sp->child_fderr, rfds);
				if (n) {
					/* read from child's STDERR
					 */
					char buf_err[0x7fff];
					DEBUG_INFO("FD_ISSET(sp->child_fderr, rfds): %i", n);
					n = read(sp->child_fderr, buf_err, sizeof(buf_err)-1);
					if (n < 0) {
						int save_errno = errno;
						assert(n == -1);
						if (errno == EINTR) {
							DEBUG_INFO("read() received an EINTR, retrying");
							continue;
						}
						DEBUG("read(), errno=%i", save_errno);
						errno = save_errno;
						perror("reading child's stderr");
						exit(1);
					}
					if (n) {
						assert(n < sizeof(buf_err) && n > 0);
						buf_err[n] = 0;
						DEBUG_INFO("received [%s] in child's stderr", buf_err);
						if (cb->consume_stderr) {
							cb->consume_stderr(sp, buf_err, n);
						} else {
							DEBUG_INFO("received data is going nowhere");
						}
					} else {
						DEBUG_INFO("child's fderr got EOF");
						FD_CLR(sp->child_fderr, rfds);
						subprocess_close_child_fderr(sp);
						assert(sp->child_fderr == -1);
					}
				}
			}

			/* sometimes stdout and stderr get closed by
			 * child process (subprocess) before
			 * selfpipe[0] gets ready
			 */

			if (sp->child_fdout == -1 && sp->child_fderr == -1) {
				/* both pipes got EOF.. humm.. maybe
				 * child is not alive anymore
				 */
				DEBUG_INFO("got EOF from fdout and fderr, has child process %i gone?", sp->pid);
				if (subprocess_wait(sp, 15)) {
					break;
				}
			}

			/* rfds/selfpipe[0]
			 */

			if (selfpipe[0] != -1 && (n = FD_ISSET(selfpipe[0], rfds))) {
				DEBUG_INFO("FD_ISSET(selfpipe[0], rfds): %i", n);
				assert(subprocess_read_selfpipe() == 0);
				if (sp->is_gone) break;
			}

			/* wfds/sp->child_fdin, can we feed child's stdin? flow control
			 */

		BURST:
			if (sp->child_fdin != -1 && cb->produce_stdin) {
				FD_SET(sp->child_fdin, wfds);
				tv.tv_sec = 0;
				tv.tv_usec = cb->ptimeout;
				r = select(sp->child_fdin+1, NULL, wfds, NULL, &tv);
				if (r == -1) {
					int save_errno = errno;
					if (errno == EINTR) {
						DEBUG_INFO("select() received an EINTR, resuming main loop");
						continue;
					}
					DEBUG("select(), errno=%i", save_errno);
					errno = save_errno;
					perror("select()");
					exit(1);
				} else if (r == 0) {
					DEBUG_INFO("produce timeout happend after %i milliseconds (or buffer filled)", cb->ptimeout);
					if (cb->produce_timeout) {
						int rc;
						if ((rc = cb->produce_timeout(sp))) {
							if (rc == SIGTERM || rc == SIGKILL) {
								DEBUG_INFO("send signal %i to pid %i after sp->produce_timeout()", rc, sp->pid);
								kill(sp->pid, rc);
							} else {
								DEBUG_INFO("exiting loop due produce timeout");
							}
							break;
						}
					}
				} else {
					assert(r == 1);
					assert(FD_ISSET(sp->child_fdin, wfds));
					if (cb->produce_stdin(sp, sp->child_fdin)) {
						goto BURST;
					}
				}
			}
		} else {
			/* consume timeout happend (for both child's stdout and stderr)
			 */
			DEBUG_INFO("consume timeout happend after %i milliseconds", cb->ctimeout);
			if (cb->consume_timeout) {
				int rc;
				FD_SET(sp->child_fdin, wfds);
				tv.tv_sec = 0;
				tv.tv_usec = 0;
				r = select(sp->child_fdin+1, NULL, wfds, NULL, &tv);
				if (r == -1) {
					int save_errno = errno;
					if (errno == EINTR) {
						DEBUG_INFO("select() received an EINTR, resuming main loop");
						continue;
					}
					DEBUG("select(), errno=%i", save_errno);
					errno = save_errno;
					perror("select()");
					exit(1);
				}
				assert(r == 0 || (r == 1 && FD_ISSET(sp->child_fdin, wfds)));
				if ((rc = cb->consume_timeout(sp, sp->child_fdin, r))) {
					if (rc == SIGTERM || rc == SIGKILL) {
						DEBUG_INFO("send signal %i to pid %i after sp->consume_timeout()", rc, sp->pid);
						kill(sp->pid, rc);
					} else {
						DEBUG_INFO("exiting loop due consume timeout");
					}
					break;
				}
			}
		}
	}

	return 0;
}

/* same fork semantics
 */
int subprocess_fork0(struct subprocess *sp)
{
	int child_stdin[2] = {0, 0};
	int child_stdout[2] = {0, 0};
	int child_stderr[2] = {0, 0};

	assert(sp);

	assert(selfpipe_setup() == 0);

	/* cleanup for next execution
	 */

	sp->exit_status = 0;
	sp->pid = 0;
	sp->waitpid_pid = 0;
	sp->is_gone = 0;

	/* pipe: read from [0], write to [1]
	 */

	assert(xpipe(child_stdin) == 0);
	assert(xpipe(child_stdout) == 0);
	assert(xpipe(child_stderr) == 0);

	DEBUG_INFO("child_stdin, child_stdout and child_stderr pipes created");

	/* flush prior to fork, so child does not inherit pending data
	 */

	fflush(stdout);
	fflush(stderr);

	/* fork
	 */

	if ((sp->pid = fork()) == 0) {
		/* prepare standard file descriptors
		 */
		assert(dup2(child_stdin[0], STDIN_FILENO) == STDIN_FILENO);
		assert(dup2(child_stdout[1], STDOUT_FILENO) == STDOUT_FILENO);
		assert(dup2(child_stderr[1], STDERR_FILENO) == STDERR_FILENO);

		/* close parent fds
		 */
		assert(close(child_stdin[1]) == 0);
		assert(close(child_stdout[0]) == 0);
		assert(close(child_stderr[0]) == 0);

		/* close old and already duplicated child fds
		 */
		assert(close(child_stdin[0]) == 0);
		assert(close(child_stdout[1]) == 0);
		assert(close(child_stderr[1]) == 0);

		return 0;
	} else if (sp->pid == -1) {
		perror("fork()");
		return -1;
	}

	DEBUG_INFO("parent pid: %i", (int) getpid());
	DEBUG_INFO("child pid:  %i", (int) sp->pid);

	/* close fds that doesn't concern to us
	 */
	assert(xclose(child_stdin[0]) == 0);
	assert(xclose(child_stdout[1]) == 0);
	assert(xclose(child_stderr[1]) == 0);

	/* set reading ends to non-blocking, i don't want
	 * surprises caused by race-conditions, there maybe
	 * little problem since we test readiness based on
	 * select
	 */

	sp->child_fdin = child_stdin[1];
	sp->child_fdout = child_stdout[0];
	sp->child_fderr = child_stderr[0];

	assert(make_fd_non_blocking(sp->child_fdin) == 0);
	assert(make_fd_non_blocking(sp->child_fdout) == 0);
	assert(make_fd_non_blocking(sp->child_fderr) == 0);

	DEBUG_INFO("sp->child_fdin=%i", sp->child_fdin);
	DEBUG_INFO("sp->child_fdout=%i", sp->child_fdout);
	DEBUG_INFO("sp->child_fderr=%i", sp->child_fderr);

	children_insert(subprocesses, child_new(sp));

	return sp->pid;
}

/* 0 on success, -1 on error, calls execve, child does not return
 */
int subprocess_fork(struct subprocess *sp)
{
	int pid;

	assert(sp);
	assert(sp->argv);
	assert(sp->argv[0]);

	if (sp->envp && sp->search_path) {
		DEBUG_INFO("it is not allowed to use search path while specifying environment variables");
		return -1;
	}

	if ((pid = subprocess_fork0(sp)) == 0) {
		char buf[1024];
		char *execve_frontend = NULL;

		/* execute
		 */
		if (sp->envp) {
			execve(sp->argv[0], sp->argv, sp->envp);
			execve_frontend = "execve";
		} else if (sp->search_path) {
			execvp(sp->argv[0], sp->argv);
			execve_frontend = "execvp";
		} else {
			execv(sp->argv[0], sp->argv);
			execve_frontend = "execv";
		}

		snprintf(buf, sizeof(buf), "%s(%s)", execve_frontend, sp->argv[0]);
		perror(buf);
		fflush(stderr);

		close(STDIN_FILENO);
		close(STDOUT_FILENO);
		close(STDERR_FILENO);

		abort();
	} else if (sp->pid == -1) {
		perror("fork");
		return -1;
	}

	return 0;
}

int subprocess_terminate(struct subprocess *sp)
{
	DEBUG_INFO("subprocess_terminate(pid=%i)", sp->pid);

	assert(sp->waitpid_pid == 0); /* you can't terminated a terminated process */

	/* this is required to release children dictionary
	 */
	assert(subprocess_read_selfpipe() == 0);

	/* close child stdin (this clearly tells child that we don't
	 * want to produce anymore)
	 */
	subprocess_close_child_fdin(sp);

	/* close reading ends, so that child get a SIGPIPE
	 */
	subprocess_close_child_fdout(sp);
	subprocess_close_child_fderr(sp);

	if ((sp->waitpid_pid = waitpid(sp->pid, &sp->exit_status, WNOHANG))) {
		assert(sp->waitpid_pid == sp->pid);
	} else {
		assert(sp->waitpid_pid == 0);
		if (subprocess_wait(sp, 5000 /* 5 seconds */) == 0) {
			/* we tried to be nice, giving hints to subprocess to
			 * stop its job
			 */
			DEBUG_INFO("send SIGTERM to child pid %i", sp->pid);
			assert(kill(sp->pid, SIGTERM) == 0);
			if (subprocess_wait(sp, 5000 /* 5 seconds */) == 0) {
				/* still alive, this is the last resort, we must
				 * retry SIGKILL indefinitely
				 */
#ifdef DEBUG_INFO_ENABLED
				int tries = 0;
				DEBUG_INFO("still alive after 5 seconds with kill(child_pid=%i, SIGTERM)", sp->pid);
				DEBUG_INFO("send SIGKILL to child pid %i", sp->pid);
#endif
				for (;;) {
					/* we can't go, if we go, we
					 * run the risk of resource
					 * exhaustion for piling up
					 * immortal processes
					 */
					int r;
					assert(kill(sp->pid, SIGKILL) == 0);
					if ((r = subprocess_wait(sp, 10000 /* 10 seconds */))) {
						DEBUG_INFO("child %i is finally gone, after %i tries", r, tries);
						break;
					}
					DEBUG_INFO("still alive after 10 seconds with kill(child_pid=%i, SIGKILL), try number %i, i won't give up", sp->pid, ++tries);
				}
			}
		}

		/* the child has terminated, that's for sure, we do a
		 * waitpid here just to avoid zombie (defunct)
		 * processes and to get the proper exit status
		 */
		assert((sp->waitpid_pid = waitpid(sp->pid, &sp->exit_status, WNOHANG)) == sp->pid);
	}

	if (sp->is_gone == 0) {
		/* some SIGCHLD may be lost if they arrive at same
		 * time (see test-subprocess-13.c)
		 */
		struct child *c;

		DEBUG_INFO("selfpipe failed for pid %i", sp->pid);

		assert(c = children_search_by_pid(subprocesses, sp->pid));
		DEBUG_INFO("\x20\x20pid %i found at child %p", sp->pid, c);

		children_remove(subprocesses, c);

		child_free(c);
		c = NULL;

		sp->is_gone = 1;
	}

	return 0;
}

void subprocess_exit_debug(struct subprocess *sp)
{
	int status = sp->exit_status;

	assert(sp->pid > 0);
	assert(sp->waitpid_pid == sp->pid); /* must be terminated */

	if (WIFEXITED(status)) {
		if (WEXITSTATUS(status) != EXIT_SUCCESS) {
			DEBUG_INFO("child %i exited with error, exit status = %i", sp->pid, WEXITSTATUS(status));
		} else {
			DEBUG_INFO("child %i exited with EXIT_SUCCESS", sp->pid);
		}
	} else if (WIFSIGNALED(status)) {
		printf("child %i was killed by signal %d\n", sp->pid, WTERMSIG(status));
	} else if (WIFSTOPPED(status)) {
		printf("child %i was stopped by signal %d\n", sp->pid, WSTOPSIG(status));
#ifdef WIFCONTINUED
	} else if (WIFCONTINUED(status)) {
		printf("continued\n");
#endif
	}
}

int subprocess_run(struct subprocess *sp, struct subprocess_callbacks *cb)
{
	int r;

	/* fork
	 */
	assert(subprocess_fork(sp) == 0);

	/* process child communication
	 */
	r = loop_child_pipes(sp, cb);
	DEBUG_INFO("loop_child_pipes() done, returned %i, pid=%i", r, sp->pid);
	if (r) {
		/* error while processing child communication
		 */
		exit(1);
	}

	assert(subprocess_terminate(sp) == 0);

	return 0;
}
