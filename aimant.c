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
 * reference: http://wiki.nginx.org/LogRotation
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <ctype.h>

#include "subprocess.h"
#include "str.h"
#include "item.h"

#include "debug0.h"

#include "bsd-getopt_long.h"
#include "getopt_x.h"

#include "aimant.h"

#define MAXLINE 10000

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

//#define SIMULATE_PARTIAL_SINK_FEED

/* "create the sink, then the tap, close the tap and drain the sink"
 */

#define MAX2(a, b) ((a) >= (b) ? (a) : (b))
#define MAX3(a, b, c) MAX2(MAX2(a, b), c)

#define DELTA_USEC(a,b) (((a)->tv_sec - (b)->tv_sec) * 1000000 + (a)->tv_usec - (b)->tv_usec)
#define DELTA_MSEC(a,b) (DELTA_USEC(a,b) / 1000)

int sink_open(struct sink *x, int search_path, char **argv)
{
	assert(x->sp->argv == NULL);
	memset(x, 0, sizeof(struct sink));
	x->sp->search_path = search_path;
	x->sp->argv = argv;
	assert(subprocess_fork(x->sp) == 0);
	x->fd = x->sp->child_fdin;
	DEBUG_INFO("sink_open(x,search_path=%i,argv=[argv[0]=[%s]]): done", search_path, argv[0]);
	return 0;
}

void sink_close(struct sink *x)
{
	assert(x->sp->pid > 0);

	/* we are no more interested in tap output
	 */
	subprocess_close_child_fdin(x->sp);

	assert(subprocess_terminate(x->sp) == 0);

	x->fd = -1;
}

int fd_tap_open(struct fd_tap *x, int fd)
{
	memset(x, 0, sizeof(struct fd_tap));
	x->fd = fd;
	assert(make_fd_non_blocking(x->fd) == 0);
	DEBUG_INFO("fd_tap_open(x,fd=%i): done", fd);
	return 0;
}

void fd_tap_close(struct fd_tap *x)
{
	assert(x->fd >= 0);
	close(x->fd);
	x->fd = -1;
}

#if 1
int file_tap_open(struct file_tap *x, const char *path)
{
	assert(x->path->s == NULL);
	memset(x, 0, sizeof(struct file_tap));
	str_copyz(x->path, path);
	x->fd = open(x->path->s, O_RDONLY);
	assert(x->fd > 0);
	assert(make_fd_non_blocking(x->fd) == 0);
	DEBUG_INFO("file_tap_open(x,path=[%s]): done", path);
	return 0;
}

void file_tap_close(struct file_tap *x)
{
	assert(x->path->len);
	assert(x->fd >= 0);
	close(x->fd);
	x->fd = -1;
	str_free(x->path);
}
#endif

static int tail_fn0(char *filename);
static int tail_f(char *filename);

int cat_tap_open(struct cat_tap *x, const char *path, int seek_end)
{
	int pid;

	assert(x->path->s == NULL);
	assert(x->sp->argv == NULL); /* not used since we won't execve */
	memset(x, 0, sizeof(struct cat_tap));

	str_copyz(x->path, path);

	if ((pid = subprocess_fork0(x->sp)) == 0) {
		/* child
		 */
		int i;
		for (i = getdtablesize(); i >= 0; i--) {
			if (i <= STDERR_FILENO) continue;
			close(i);
		}
		if (seek_end) {
		  assert(tail_fn0(x->path->s) == 0);
		} else {
		  assert(tail_f(x->path->s) == 0);
		}
		exit(0);
	} else if (pid < 0) {
		return -1;
	}

	x->fd = x->sp->child_fdout;

	DEBUG_INFO("cat_tap_open(x, path=[%s]): done, pid=%i", path, x->sp->pid);

	return 0;
}

void cat_tap_close(struct cat_tap *x)
{
	assert(x->sp->pid > 0);
	assert(x->sp->argv == NULL);

	/* we are no more interested in tap output
	 */
	subprocess_close_child_fdout(x->sp);
	subprocess_close_child_fderr(x->sp);

	DEBUG_INFO("sending SIGTERM to cat_tap pid %i", x->sp->pid);
	assert(kill(x->sp->pid, SIGTERM) == 0);

	assert(subprocess_terminate(x->sp) == 0);

	x->fd = -1;
	str_free(x->path);
}

int fd_tap_read(struct fd_tap *x, void *buf, int bufsz)
{
	int n = read(x->fd, buf, bufsz);
	if (n < 0) {
		int save_errno = errno;
		assert(n == -1);
		if (errno == EAGAIN) return -1;
		if (errno == EINTR) return -1;
		DEBUG("read(), errno=%i", save_errno);
		errno = save_errno;
		perror("read()");
		exit(1);
	}
	if (n) {
		x->bytes_read += n;
		//get_current_timeval(x->time_read);
	} else {
		assert(x->got_eof == 0);
		x->got_eof = 1;
	}
	return n;
}

#if 1
int file_tap_read(struct file_tap *x, void *buf, int bufsz)
{
	int n = read(x->fd, buf, bufsz);
	if (n < 0) {
		int save_errno = errno;
		assert(n == -1);
		if (errno == EAGAIN) return -1;
		if (errno == EINTR) return -1;
		DEBUG("read(), errno=%i", save_errno);
		errno = save_errno;
		perror("read()");
		exit(1);
	}
	if (n) {
		x->bytes_read += n;
		//get_current_timeval(x->time_read);
	} else {
		assert(x->got_eof == 0);
		x->got_eof = 1;
	}
	return n;
}
#endif

int cat_tap_read(struct cat_tap *x, void *buf, int bufsz)
{
	int n = read(x->fd, buf, bufsz);
	if (n < 0) {
		int save_errno = errno;
		assert(n == -1);
		if (errno == EAGAIN) return -1;
		if (errno == EINTR) return -1;
		DEBUG("read(), errno=%i", save_errno);
		errno = save_errno;
		perror("read()");
		exit(1);
	}
	if (n) {
		x->bytes_read += n;
		//get_current_timeval(x->time_read);
	} else {
		assert(x->got_eof == 0);
		x->got_eof = 1;
	}
	return n;
}

#define BUFFER_ID_TAP_STDIN 1
#define BUFFER_ID_TAP_CURRENT 2
#define BUFFER_ID_TAP_HANGING_NORMAL 3
#define BUFFER_ID_TAP_HANGING_SETTLE 4

DEFINE_ITEM(buffer,
	    int id;
	    struct str buf[1];
	    int pos; /* buffer position */
  );

DEFINE_QUEUE(buffer_queue, buffer);

struct buffer *buffer_new(struct buffer *tail, int id, void *buf, int bufsz)
{
	struct buffer *r = buffer_new0(tail);
	r->id = id;
#if 0
	DEBUG_INFO("%i -> %i", r->_position, tail ? tail->_position : -1);
	str_copyf(r->buf, "%s got %i bytes\n", id, bufsz);
#else
	str_copyn(r->buf, buf, bufsz);
#endif
	r->pos = 0;
	return r;
}

void buffer_free(struct buffer *x)
{
	while (x) {
		struct buffer *t = x->tail;
		str_free(x->buf);
		free(x);
		x = t;
	}
}

void buffer_queue_free(struct buffer_queue *x)
{
	if (x) {
		buffer_free(x->enqueue);
		buffer_free(x->dequeue);
		free(x);
	}
}

/* same write semantics
 */
int sink_write(struct sink *x, struct buffer *b)
{
	int n;

	/* since we are using non-blocking io, try to write directly
	 */
	assert(x->sp->child_fdin >= 0);
	assert(x->sp->child_fdin == x->fd);

#ifdef SIMULATE_PARTIAL_SINK_FEED
	if (b->pos == 0) {
		n = write(x->fd, b->buf->s, b->buf->len / 2);
	} else {
		n = write(x->fd, b->buf->s + b->pos, b->buf->len - b->pos);
	}
#else
	n = write(x->fd, b->buf->s + b->pos, b->buf->len - b->pos);
#endif
	if (n < 0) {
		int save_errno = errno;
		assert(n == -1);
		if (errno == EAGAIN) return -1;
		if (errno == EINTR) return -1;
		if (errno == EPIPE) {
			DEBUG("sink pid=%i got EPIPE", x->sp->pid);
			if (x->sp->child_fderr >= 0) {
				char buf[4096];
				int n;
				if ((n = read(x->sp->child_fderr, buf, sizeof(buf)-1)) > 0) {
					buf[n] = 0;
					DEBUG("sink pid=%i stderr=[%s]", x->sp->pid, buf);
				}
			}
			errno = save_errno;
			return -1;
		}
		DEBUG("write(), errno=%i", save_errno);
		errno = save_errno;
		perror("write()");
		exit(1);
	}
	if (n) {
		b->pos += n;
	} else {
		DEBUG_INFO("sink pid=%i got EOF", x->sp->pid);
		assert(x->got_eof == 0);
		x->got_eof = 1;
	}
	return n;
}

/* same write semantics (except for EOF, must use x->got_eof), EAGAIN
 * and EINTR are ignored
 */
int sink_write_from_queue(struct sink *x, struct buffer_queue *q)
{
	struct buffer *b;
	int total = 0;
	while ((b = buffer_queue_dequeue(q))) {
		int n;
		n = sink_write(x, b);
		if (n < 0) {
			int save_errno = errno;
			assert(n == -1);
			if (errno == EAGAIN) {
				DEBUG_INFO("sink is busy. how many buffers remaining? %i", buffer_queue_len(q));
				break;
			}
			if (errno == EINTR) {
				DEBUG_INFO("sink got interrupted (EINTR). how many buffers remaining? %i", buffer_queue_len(q));
				break;
			}
			/* errors beyond recovery
			 */
			q->dequeue = buffer_tail0(b, q->dequeue); /* reschedule */
			if (errno == EPIPE) {
				DEBUG("sink got unrecoverable error (EPIPE - broken pipe)");
				errno = save_errno;
				return -1;
			}
			DEBUG("sink_write(), errno=%i", save_errno);
			errno = save_errno;
			perror("sink_write()");
			return -1;
		} else if (n) {
			total += n;
			if (b->pos < b->buf->len) {
				DEBUG_INFO("sink consumed (so far) %i bytes of %i, %i buffers enqueued",
				      b->pos, b->buf->len, buffer_queue_len(q));
				break;
			}
			DEBUG_INFO("sink consumed all %i bytes, %i buffers enqueued", b->pos, buffer_queue_len(q));
			/* discard consumed buffer
			 */
			buffer_free(b);
			b = NULL;
		} else {
			assert(x->got_eof);
			break;
		}
	}
	if (b) q->dequeue = buffer_tail0(b, q->dequeue); /* reschedule */
	return total;
}

/* return 0 on success, on error, -1 is returned, and errno is set
 * appropriately, EAGAIN on timeout
 */
int wait_til_ready(int fd, int msec, int ready_for_read)
{
	int eintr_count = 0;
	fd_set fds[1], *prfds = NULL, *pwfds = NULL;
	struct timeval start[1];
	struct timeval tv[1];
	long dmsec;

	get_current_timeval(start);

	DEBUG_INFO("wait_til_ready(fd=%i, msec=%i, ready_for_read=%i) begin", fd, msec, ready_for_read);

	FD_ZERO(fds);

	if (ready_for_read) prfds = fds;
	else pwfds = fds;

	for (;;) {
		int r;

		FD_SET(fd, fds);

		if (msec) {
			struct timeval current[1];
			get_current_timeval(current);
			if ((dmsec = DELTA_MSEC(current, start)) > msec) {
				DEBUG("wait_til_ready(): timeout (time elapsed)");
				errno = EAGAIN;
				return -1;
			}
			tv->tv_sec = 0;
			tv->tv_usec = (msec - dmsec) * 1000;
		} else {
			dmsec = 0;
			tv->tv_sec = 0;
			tv->tv_usec = 0;
		}

		DEBUG_INFO("wait_til_ready(): select(fd+1) for %li milliseconds", msec - dmsec);
		if ((r = select(fd+1, prfds, pwfds, NULL, tv)) < 0) {
			int save_errno = errno;
			assert(r == -1);
			if (errno == EINTR) {
				eintr_count++;
				if (eintr_count == 10) {
					DEBUG("wait_til_ready(): too much EINTR, giving up select");
					errno = save_errno;
					return -1;
				}
				DEBUG_INFO("wait_til_ready(): got EINTR");
				continue;
			}
			DEBUG("wait_til_ready(): select(), errno=%i", save_errno);
			errno = save_errno;
			perror("select()");
			return -1;
		} else if (r) {
			DEBUG_INFO("wait_til_ready(): ready");
			break;
		}
		DEBUG("wait_til_ready(): timeout");
		errno = EAGAIN;
		return -1;
	}

	DEBUG_INFO("wait_til_ready(fd=%i, msec=%i, ready_for_read=%i) done", fd, msec, ready_for_read);

	return 0;
}

int wait_til_ready_for_read(int fd, int msec)
{
	return wait_til_ready(fd, msec, 1 /* ready_for_read */);
}

int wait_til_ready_for_write(int fd, int msec)
{
	return wait_til_ready(fd, msec, 0 /* ready_for_read */);
}

/* same write semantics (except for EOF, must use x->got_eof), EAGAIN
 * and EINTR are ignored
 */
int sink_flush_all_buffers(struct sink *x, struct buffer_queue *q)
{
	int r;
	int total = 0;
	DEBUG_INFO("sink_flush_all_buffers(x=[pid=%i], q=[buffers=%i]) begin", x->sp->pid, buffer_queue_len(q));
	while (buffer_queue_len(q)) {
		int n;
		n = sink_write_from_queue(x, q);
		if (n < 0) {
			int save_errno = errno;
			assert(n == -1);
			DEBUG("sink_flush_all_buffers(x=[pid=%i]), errno=%i", x->sp->pid, save_errno);
			errno = save_errno;
			perror("sink_write_from_queue()");
			return -1;
		}
		total += n;
		if (x->got_eof) break;
		if ((r = wait_til_ready_for_write(x->fd, 5000 /* 5 secs */))) {
			int save_errno = errno;
			assert(r == -1);
			DEBUG("wait_til_ready_for_write(x->fd=%i), errno=%i", x->fd, save_errno);
			errno = save_errno;
			perror("wait_til_ready_for_write(x->fd)");
			return -1;
		}
	}
	DEBUG_INFO("sink_flush_all_buffers(x=[pid=%i,eof=%i], q=[buffers=%i]) done", x->sp->pid, x->got_eof, buffer_queue_len(q));
	return total;
}

static int enqueue_til_settle(struct buffer_queue *q, struct cat_tap *input, int id, int msec_to_settle, char *buf, int bufsz)
{
	fd_set rfds[1];
	struct timeval tv[1];
	int r, bytes_read;
	int bql_before = buffer_queue_len(q);

	assert(input->fd >= 0);

	DEBUG_INFO("enqueue_til_settle: queue len=%i, input->fd=%i, msec_to_settle=%i", bql_before, input->fd, msec_to_settle);

	FD_ZERO(rfds);

	for (;;) {
		FD_SET(input->fd, rfds);

		tv->tv_sec = 0;
		tv->tv_usec = msec_to_settle * 1000;

		r = select(input->fd + 1, rfds, NULL, NULL, tv);
		if (r < 0) {
			int save_errno = errno;
			assert(r == -1);
			if (errno == EINTR) {
				DEBUG_INFO("select() received an EINTR, retrying");
				continue;
			}
			DEBUG("select(), errno=%i", save_errno);
			errno = save_errno;
			perror("select()");
			exit(1);
		} else if (r) {
			int n;
			n = cat_tap_read(input, buf, bufsz-1);
			if (n < 0) {
				int save_errno = errno;
				assert(n == -1);
				if (errno == EAGAIN) {
					DEBUG_INFO("cat_tap_read(input, buf, bufsz-1) got EAGAIN");
					continue;
				}
				if (errno == EINTR) {
					DEBUG_INFO("cat_tap_read(input, buf, bufsz-1) got EINTR");
					continue;
				}
				DEBUG("cat_tap_read(), errno=%i", save_errno);
				errno = save_errno;
				perror("cat_tap_read()");
				exit(1);
			}
			if (n) {
				buf[n] = 0;
				q->enqueue = buffer_new(q->enqueue, id, buf, n);
				DEBUG_INFO("enqueue_til_settle: enqueued %i bytes", n);
				bytes_read += n;
			} else {
				DEBUG_INFO("enqueue_til_settle: input->fd got EOF, something went wrong");
				assert(input->got_eof);
				break;
			}
			if (buffer_queue_len(q) - bql_before >= 100) {
				DEBUG("enqueue_til_settle: input tap is not settling, exiting with %i buffers enqueued and errno=EAGAIN to avoid resource exhaustion", buffer_queue_len(q));
				errno = EAGAIN;
				return -1;
			}
		} else {
			/* timeout
			 */
			DEBUG_INFO("enqueue_til_settle: settled (timeout happened), new queue size is %i", buffer_queue_len(q));
			break;
		}
	}

	return 0;
}

static int doit(int pid_to_send_signal, struct sink *svlogd, struct fd_tap *fd0, const char *input_path, long count_to_rotate, int exit_on_timeout)
{
	fd_set rfds[1], *prfds, wfds[1], *pwfds;
	struct timeval tv[1];
	int r;
	struct cat_tap input0[1];
	struct cat_tap input1[1];
	struct buffer_queue *q;
	char *buf;
	int bufsz = 0x100000 /* 1048576 */;
	int current_input = 0; /* 0=input0, 1=input1 */
	struct cat_tap *inputs[2] = {input0, input1};
	DEFINE_STR(hanging_path);
	int producer_is_gone = 0;

	buf = malloc(bufsz);
	assert(buf);

	memset(input0, 0, sizeof(input0));
	memset(input1, 0, sizeof(input1));
	assert(count_to_rotate > 0);

	assert(fd0->fd >= 0);

	input0->fd = -1;
	input1->fd = -1;

	assert(svlogd->sp->pid > 0);

	assert(cat_tap_open(input0, input_path, 1 /* seek end */) == 0);

	str_copyz(hanging_path, input_path);
	str_catz(hanging_path, ".hanging");

	q = buffer_queue_new0();

	FD_ZERO(rfds);
	FD_ZERO(wfds);

	for (;;) {
		int max_fds = -1;
		int selfpipe = subprocess_get_selfpipe_read_fd();
		struct cat_tap *input_current = inputs[current_input];
		struct cat_tap *input_hanging = inputs[(current_input + 1) % 2];
		int bql = buffer_queue_len(q);
		int bytes_read = 0;
		int bytes_written = 0;

		assert(selfpipe >= 0);

		if (bql < 100) {
			if (fd0->fd != -1) {
				FD_SET(fd0->fd, rfds);
				max_fds = MAX2(fd0->fd, max_fds);
			}
			if (input_current->fd != -1) {
				FD_SET(input_current->fd, rfds);
				max_fds = MAX2(input_current->fd, max_fds);
			}
			if (input_hanging->fd != -1) {
				FD_SET(input_hanging->fd, rfds);
				max_fds = MAX2(input_hanging->fd, max_fds);
			}
			prfds = rfds;
		} else {
			DEBUG_INFO("too many buffers enqueued (%i buffers), suspending tap", bql);
			prfds = NULL;
		}

		FD_SET(selfpipe, rfds);
		max_fds = MAX2(selfpipe, max_fds);
		prfds = rfds;

		if (bql) {
			if (svlogd->fd != -1) {
				FD_SET(svlogd->fd, wfds);
				max_fds = MAX2(svlogd->fd, max_fds);
			}
			pwfds = wfds;
		} else {
			pwfds = NULL;
		}

		if (fd0->got_eof) {
			/* our stdin is the way to sign a clean
			 * exit
			 */
			tv->tv_sec = 3;
			tv->tv_usec = 0;
		} else if (producer_is_gone) {
			/* our producer is gone, we can't sign log
			 * rotation anymore (the producer process is
			 * not necessarily the other end of our stdin,
			 * indeed, this is the reason for the creation
			 * of this program, to allow such thing)
			 */
			tv->tv_sec = 3;
			tv->tv_usec = 0;
		} else {
			tv->tv_sec = 5;
			tv->tv_usec = 0;
		}

		DEBUG_INFO("select(max_fds=%i) timeout is %li seconds and %li useconds (%li milliseconds)", max_fds, (long)tv->tv_sec, (long)tv->tv_usec, (long)tv->tv_usec/1000);

		assert(max_fds >= 0);

		r = select(max_fds+1, prfds, pwfds, NULL, tv);
		if (r < 0) {
			int save_errno = errno;
			assert(r == -1);
			if (errno == EINTR) {
				DEBUG_INFO("select() received an EINTR, retrying");
				continue;
			}
			DEBUG("select(), errno=%i", save_errno);
			errno = save_errno;
			perror("select()");
			exit(1);
		} else if (r) {
			int n;

			if (selfpipe != -1 && FD_ISSET(selfpipe, rfds)) {
				DEBUG_INFO("selpipe is read");
				assert(subprocess_read_selfpipe() == 0);
				if (svlogd->sp->is_gone) {
					char buf[4096];
					int n;
					DEBUG_INFO("svlogd has gone unexpectedly");
					assert(svlogd->sp->child_fderr >= 0);
					if ((n = read(svlogd->sp->child_fderr, buf, sizeof(buf)-1)) > 0) {
						buf[n] = 0;
						DEBUG_INFO("svlogd stderr=[%s]", buf);
					}
					break;
				}
			}

			if (prfds) {
				if (fd0->fd != -1 && FD_ISSET(fd0->fd, rfds)) {
					n = fd_tap_read(fd0, buf, bufsz-1);
					if (n < 0) {
						int save_errno = errno;
						assert(n == -1);
						if (errno == EAGAIN) {
							DEBUG_INFO("fd_tap_read(fd0, buf, bufsz-1) got EAGAIN");
							continue;
						}
						if (errno == EINTR) {
							DEBUG_INFO("fd_tap_read(fd0, buf, bufsz-1) got EINTR");
							continue;
						}
						DEBUG("fd_tap_read(), errno=%i", save_errno);
						errno = save_errno;
						perror("fd_tap_read()");
						exit(1);
					}
					if (n) {
						buf[n] = 0;
						q->enqueue = buffer_new(q->enqueue, BUFFER_ID_TAP_STDIN, buf, n);
						DEBUG_INFO("enqueued %i bytes from stdin", n);
						bytes_read += n;
					} else {
						DEBUG_INFO("fd0->fd got EOF, must do a clean exit");
						assert(fd0->got_eof);
						FD_CLR(fd0->fd, rfds);
						fd_tap_close(fd0);
						assert(fd0->fd == -1);

						/* close taps, we are
						 * finishing
						 */
						if (input_hanging->fd >= 0) {
							DEBUG_INFO("closing input_hanging tap, pid=%i", input_hanging->sp->pid);
							FD_CLR(input_hanging->fd, rfds);
							cat_tap_close(input_hanging);
						}
						if (input_current->fd >= 0) {
							DEBUG_INFO("closing input_current tap, pid=%i", input_current->sp->pid);
							FD_CLR(input_current->fd, rfds);
							cat_tap_close(input_current);
						}

						assert(input_hanging->fd == -1);
						assert(input_current->fd == -1);
					}
				}

				if (input_hanging->fd != -1 && FD_ISSET(input_hanging->fd, rfds)) {
					n = cat_tap_read(input_hanging, buf, bufsz-1);
					if (n < 0) {
						int save_errno = errno;
						assert(n == -1);
						if (errno == EAGAIN) {
							DEBUG_INFO("cat_tap_read(input_hanging, buf, bufsz-1) got EAGAIN");
							continue;
						}
						if (errno == EINTR) {
							DEBUG_INFO("cat_tap_read(input_hanging, buf, bufsz-1) got EINTR");
							continue;
						}
						DEBUG("cat_tap_read(), errno=%i", save_errno);
						errno = save_errno;
						perror("cat_tap_read()");
						exit(1);
					}
					if (n) {
						buf[n] = 0;
						q->enqueue = buffer_new(q->enqueue, BUFFER_ID_TAP_HANGING_NORMAL, buf, n);
						DEBUG_INFO("enqueued %i bytes from hanging", n);
						bytes_read += n;
					} else {
						DEBUG_INFO("input_hanging->fd got EOF, something went wrong");
						assert(input_hanging->got_eof);
						break;
					}
				}

				if (input_current->fd != -1 && FD_ISSET(input_current->fd, rfds)) {
					n = cat_tap_read(input_current, buf, bufsz-1);
					if (n < 0) {
						int save_errno = errno;
						assert(n == -1);
						if (errno == EAGAIN) {
							DEBUG_INFO("cat_tap_read(input_current, buf, bufsz-1) got EAGAIN");
							continue;
						}
						if (errno == EINTR) {
							DEBUG_INFO("cat_tap_read(input_current, buf, bufsz-1) got EINTR");
							continue;
						}
						DEBUG("cat_tap_read(), errno=%i", save_errno);
						errno = save_errno;
						perror("cat_tap_read()");
						exit(1);
					}
					if (n) {
						buf[n] = 0;
						q->enqueue = buffer_new(q->enqueue, BUFFER_ID_TAP_CURRENT, buf, n);
						DEBUG_INFO("enqueued %i bytes from current", n);
						bytes_read += n;
					} else {
						DEBUG_INFO("input_current->fd got EOF, something went wrong");
						assert(input_current->got_eof);
						break;
					}
				}

				if (input_current->bytes_read >= count_to_rotate) {

					DEBUG_INFO("read %i bytes from [%s], hanging it (limit is %li)", input_current->bytes_read, input_current->path->s, count_to_rotate);

					/* rename current to hanging path
					 */

					if ((r = rename(input_current->path->s, hanging_path->s))) {
						int save_errno = errno;
						assert(r == -1);
						DEBUG("rename(input_current->path->s=[%s], hanging_path->s=[%s]), errno=%i", input_current->path->s, hanging_path->s, save_errno);
						errno = save_errno;
						perror(hanging_path->s);
						break;
					}

					/* create current path
					 */

					{
						int fd;
						if ((fd = open(input_path, O_WRONLY | O_CREAT, 0644)) < 0) {
							int save_errno = errno;
							assert(fd == -1);
							DEBUG("open(input_path=[%s], O_WRONLY | O_CREAT, 0644), errno=%i", input_path, save_errno);
							errno = save_errno;
							perror(input_path);
							break;
						}
						assert(close(fd) == 0);
						fd = -1;
					}

					DEBUG_INFO("renamed [%s] to [%s] and created former", input_current->path->s, hanging_path->s);

					/* update input path
					 * (input_current will be
					 * input_hanging in next
					 * round)
					 */

					str_copy(input_current->path, hanging_path);

					/* close old hanging path
					 */

					if (input_hanging->fd >= 0) {
						DEBUG_INFO("[%s] is done", input_hanging->path->s);
						cat_tap_close(input_hanging);
					} else {
						DEBUG_INFO("first hanging");
					}

					/* reopen input (input_hanging
					 * will be input_current in
					 * next round)
					 */

					assert(cat_tap_open(input_hanging, input_path, 0 /* seek end */) == 0);

					/* send SIGUSR1 to producer
					 * process, so it can reopen
					 * it's log file
					 */

					if (kill(pid_to_send_signal, SIGUSR1) == 0) {
						DEBUG_INFO("sent SIGUSR1 to pid %i", pid_to_send_signal);
					} else {
						DEBUG_INFO("kill(pid_to_send_signal=%i, SIGUSR1=%i) failed", pid_to_send_signal, SIGUSR1);
						producer_is_gone = 1;
					}

					/* we flush all buffers here
					 * for the sake of recalling
					 * resources for the new
					 * produce/consume round, this
					 * also give producer process
					 * some time to reopen it's
					 * log file and hanging input
					 * tap to settle
					 */
					if ((n = sink_flush_all_buffers(svlogd, q)) < 0) {
						int save_errno = errno;
						assert(n == -1);
						DEBUG("sink_flush_all_buffers(svlogd=[pid=%i], q), errno=%i", svlogd->sp->pid, save_errno);
						errno = save_errno;
						perror(hanging_path->s);
						break;
					}
					if (svlogd->got_eof) {
						DEBUG_INFO("svlogd got EOF, something went wrong, wrote %i bytes before EOF though", n);
						break;
					}
					DEBUG_INFO("flushed all remaining buffers, %i bytes total", n);

					/*
					 */

					current_input = (current_input + 1) % 2;
					input_current = inputs[current_input];
					input_hanging = inputs[(current_input + 1) % 2];

					DEBUG_INFO("current_input = %i", current_input);

					/* this is just to drain
					 * pending data from pipe and
					 * maintain order
					 */

					if ((r = enqueue_til_settle(q, input_hanging, BUFFER_ID_TAP_HANGING_SETTLE, 100 /* msec to settle */, buf, bufsz)) < 0) {
						assert(r == -1);
						assert(errno == EAGAIN);
						DEBUG_INFO("hanging input tap failed to settle");
						break;
					}
					if (input_hanging->got_eof) {
						DEBUG_INFO("input_hanging->fd got EOF, something went wrong");
						break;
					}
				}
			}

			if (pwfds) {
				if (svlogd->fd != -1 && FD_ISSET(svlogd->fd, pwfds)) {
					DEBUG_INFO("svlogd is ready, %i buffers enqueued", buffer_queue_len(q));
					n = sink_write_from_queue(svlogd, q);
					if (n < 0) {
						int save_errno = errno;
						assert(n == -1);
						DEBUG("sink_write_from_queue(), errno=%i", errno);
						errno = save_errno;
						perror("sink_write_from_queue()");
						break;
					}
					DEBUG_INFO("svlogd done, %i buffers remaining", buffer_queue_len(q));
					bytes_written += n;
					if (svlogd->got_eof) {
						DEBUG_INFO("svlogd got EOF, something went wrong");
						break;
					}
				}
			}

			if (fd0->got_eof) {
				if (bytes_read || bytes_written) {
					DEBUG_INFO("fd0 is closed, but we may have pending data");
				} else {
					DEBUG_INFO("fd0 is closed and we have no pending data");
					break;
				}
			} else if (producer_is_gone) {
				if (bytes_read || bytes_written) {
					DEBUG_INFO("producer is gone, but we may have pending data");
				} else {
					DEBUG_INFO("producer is gone, and we have no pending data");
					break;
				}
			}
		} else { /* matches:	} else if (r) { */
			if (fd0->got_eof) {
				DEBUG_INFO("fd0 is closed and got timeout, sink failed to drain remaining data, exiting loop, %i buffers were left", buffer_queue_len(q));
				break;
			} else if (producer_is_gone) {
				DEBUG_INFO("producer is gone and got timeout, sink failed to drain remaining data, exiting loop, %i buffers were left", buffer_queue_len(q));
				break;
			} else {
				if (exit_on_timeout) {
					DEBUG_INFO("timeout (exiting due to -e option)");
					break;
				} else {
					DEBUG_INFO("timeout");
				}
			}
		}
	}

	/* cleanup
	 */

	if (input0->fd >= 0) {
		DEBUG_INFO("closing input0 tap, pid=%i", input0->sp->pid);
		cat_tap_close(input0);
		assert(input0->sp->waitpid_pid == input0->sp->pid); /* terminated */
	}

	if (input1->fd >= 0) {
		DEBUG_INFO("closing input1 tap, pid=%i", input1->sp->pid);
		cat_tap_close(input1);
		assert(input1->sp->waitpid_pid == input1->sp->pid); /* terminated */
	}

	if (input0->got_eof) subprocess_exit_debug(input0->sp);
	if (input1->got_eof) subprocess_exit_debug(input1->sp);

	buffer_queue_free(q);
	q = NULL;

	free(buf);
	buf = NULL;

	str_free(hanging_path);

	return 0;
}

/* getopt_x
 * reference: test-getopt-5.c
 */

static const char *options_short = NULL;
static const char *options_mandatory = "pl";

static struct option options_long[] = {
	{.val='p', .name="pid-file", .has_arg=1},
	{.val='l', .name="log-file", .has_arg=1},
	{.val='s', .name="svlogd", .has_arg=1},
	{.val='c', .name="count-to-rotate", .has_arg=1},
	{.val='e', .name="exit-on-timeout"},
	{.val='h', .name="help"},
	{.name=NULL}
};

struct args {
	char pid_file[256];
	char log_file[256];
	char svlogd_path[256];
	long count_to_rotate;
	int exit_on_timeout; /* useful for test */
} args[1] = {
	{
		.svlogd_path = "svlogd",
		.count_to_rotate = 0x1000000 /* 16777216 / 16M */
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
		case 'p': pos += snprintf(buf + pos, SOZ(bufsz,pos), "pid file to send signal (USR1) to reopen log files\n"); break;
		case 'l': pos += snprintf(buf + pos, SOZ(bufsz,pos), "log file to feed sink\n"); break;
		case 's': pos += snprintf(buf + pos, SOZ(bufsz,pos), "svlogd path, default is \"%s\"\n", args->svlogd_path); break;
		case 'c': pos += snprintf(buf + pos, SOZ(bufsz,pos), "count to rotate (bytes), default is %li\n", args->count_to_rotate); break;
		case 'e':
		case 'h':
			pos += snprintf(buf + pos, SOZ(bufsz,pos), "\n");
			break;
		default: pos += snprintf(buf + pos, SOZ(bufsz,pos), "undocumented\n");
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
		case 'p': strncpy_sizeof(args->pid_file, optarg); break;
		case 'l': strncpy_sizeof(args->log_file, optarg); break;
		case 's': strncpy_sizeof(args->svlogd_path, optarg); break;
		case 'c': args->count_to_rotate = atol(optarg); break;
		case 'e': args->exit_on_timeout = 1; break;
		case 'h': help(argv[0], state); exit(0);
		case -1: break;
		default:
			getopt_x_option_debug(state, c, opt);
			return -1;
		}
	} while (c != -1);
	if (args->count_to_rotate < 1) {
		DEBUG("invalid value for -c flag: %li", args->count_to_rotate);
		return -1;
	}
	return state->got_error;
}

static int read_pid(const char *pid_file, int *pid);

int main(int argc, char **argv)
{
	char **sink_argv;
	struct sink svlogd[1];
	struct fd_tap fd0[1];
	struct getopt_x state[1];
	int pid_to_send_signal = -1;

	if (process_args(state, argc, argv)) {
		help(argv[0], state);
		exit(1);
	}

	if (argc == 1) {
		help(argv[0], state);
		exit(0);
	}

	DEBUG_INFO("args->pid_file=[%s]", args->pid_file);
	DEBUG_INFO("args->log_file=[%s]", args->log_file);
	DEBUG_INFO("args->svlogd_path=[%s]", args->svlogd_path);
	DEBUG_INFO("args->count_to_rotate=%li", args->count_to_rotate);

	if (read_pid(args->pid_file, &pid_to_send_signal)) {
		DEBUG("invalid pid file");
		return 1;
	}

	memset(svlogd, 0, sizeof(svlogd));
	memset(fd0, 0, sizeof(fd0));

	/* spawn svlogd
	 */

	sink_argv = calloc(4, sizeof(char*));
	sink_argv[0] = args->svlogd_path;
	sink_argv[1] = "-ttt";
	sink_argv[2] = ".";
	sink_argv[3] = NULL;

	assert(sink_open(svlogd, 1 /* search path? */, sink_argv) == 0);

	/* register stdin as tap
	 */

	assert(fd_tap_open(fd0, STDIN_FILENO) == 0);

	/* unleash
	 */

	assert(doit(pid_to_send_signal, svlogd, fd0, args->log_file, args->count_to_rotate, args->exit_on_timeout) == 0);

	DEBUG_INFO("closing svlogd sink, pid=%i", svlogd->sp->pid);
	sink_close(svlogd);

	/* cleanup
	 */

	free(sink_argv);
	sink_argv = NULL;

	return 0;
}

static int read_exact(int fd, void *buf, int len);

#define NUMBER_OF_OPEN_TRIES 10

static int read_pid(const char *pid_file, int *pid)
{
	int fd;
	char buf[100];
	int bufsz = sizeof(buf);
	struct stat st[1];
	int open_try = 1;

	for (;;) {
		if ((fd = open(pid_file, O_RDONLY)) < 0) {
			int save_errno = errno;
			assert(fd == -1);
			if (errno == ENOENT && open_try < NUMBER_OF_OPEN_TRIES) {
				DEBUG("open(pid_file=[%s], O_RDONLY) got ENOENT (file does not exist, retrying), try %i of %i", pid_file, open_try+1, NUMBER_OF_OPEN_TRIES);
				usleep(100 * 1000);
				open_try++;
				continue;
			}
			DEBUG("open(pid_file=[%s], O_RDONLY), errno=%i", pid_file, save_errno);
			errno = save_errno;
			perror(pid_file);
			return -1;
		}
		break;
	}
	if (fstat(fd, st) < 0) {
		int save_errno = errno;
		DEBUG("fstat(fd=[%i, from [%s]], st), errno=%i", fd, pid_file, save_errno);
		errno = save_errno;
		perror(pid_file);
		return -1;
	}

	assert(st->st_size < bufsz);

	assert(read_exact(fd, buf, st->st_size) == st->st_size);
	buf[st->st_size] = 0;

	assert(close(fd) == 0);
	fd = -1;

	if (*pid) *pid = atoi(buf);

	return 0;
}

static int read_exact(int fd, void *buf, int len)
{
	int i, got=0;
	do {
		if ((i = read(fd, buf + got, len - got)) <= 0)
			return i;
		got += i;
	} while (got < len);
	return len;
}

static int write_exact(int fd, void *buf, int len)
{
	int i, wrote = 0;
	do {
		if ((i = write(fd, buf + wrote, len - wrote)) <= 0) return i;
		wrote += i;
	} while (wrote < len);
	return len;
}

static int tail0loop(int fdin, int fdout)
{
	char buf[0x100000]; /* 1M */
	int bufsz = sizeof(buf);
	int eof_count = 0;

	for (;;) {
		int n;
		if ((n = read(fdin, buf, bufsz)) < 0) {
			int save_errno = errno;
			assert(n == -1);
			DEBUG("read(fdin=%i, buf, bufsz=%i), errno=%i", fdin, bufsz, save_errno);
			errno = save_errno;
			perror("read(fdin, buf, bufsz)");
			return -1;
		} else if (n) {
			int r;
			if ((r = write_exact(fdout, buf, n)) < 0) {
				int save_errno = errno;
				assert(r == -1);
				DEBUG("write_exact(fdout=%i, buf, n=%i), errno=%i", fdout, n, save_errno);
				errno = save_errno;
				perror("write_exact(fdout, buf, n)");
				return -1;
			}
			eof_count = 0;
		} else {
			/* got EOF
			 */
			int msec;
			eof_count++;

			if (eof_count >= 100) msec = 250;
			else if (eof_count >= 20) msec = 20 * 5;
			else msec = eof_count * 5;

			usleep(msec * 1000);
		}
	}

	return 0;
}

static int tail0(char *filename, int seek_end)
{
	int fd;

	if ((fd = open(filename, O_RDONLY)) < 0) {
		int save_errno = errno;
		assert(fd == -1);
		DEBUG("open(filename=[%s], O_RDONLY), errno=%i", filename, save_errno);
		errno = save_errno;
		perror(filename);
		return -1;
	}

	if (seek_end && lseek(fd, 0, SEEK_END) < 0) {
		int save_errno = errno;
		errno = save_errno;
		DEBUG("lseek(fd=%i, 0, SEEK_END), errno=%i", fd, save_errno);
		perror("lseek(fd, 0, SEEK_END)");

		assert(close(fd) == 0);
		fd = -1;
		return 1;
	}

	tail0loop(fd, STDOUT_FILENO);

	assert(close(fd) == 0);
	fd = -1;

	return 0;
}

static int tail_fn0(char *filename)
{
	return tail0(filename, 1 /* seek end */);
}

static int tail_f(char *filename)
{
	return tail0(filename, 0 /* seek end */);
}
