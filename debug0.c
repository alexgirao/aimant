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

#include "debug0.h"

//#define USE_CLOCK_GETTIME

static void get_current_timeval(struct timeval *tv)
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

/* sub or zero */
#define SOZ(a,b) ((a) > (b) ? (a) - (b) : 0)

void debug0(char *file, int line, char *format, ...)
{
	/* TODO: put timezone or use UTC
	 */

	time_t t;
	struct tm tm[1];
	struct timeval tv[1];
	va_list valist1;
	char buf[0x7fff];
	int n;
	long usec;

	va_start(valist1, format);

	get_current_timeval(tv);

	t = tv->tv_sec;
	usec = tv->tv_usec;

	assert(localtime_r(&t, tm) == tm);

	/* the time resolution (1/100 of millisecond) below is the
	 * same used by svlogd with -ttt option
	 */

	n = 0;
	n = snprintf(buf+n, SOZ(sizeof(buf),n),
		     "%u-%.2u-%.2uT%.2u:%.2u:%.2u.%.5u: %s:%i: ",
		     tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
		     tm->tm_hour, tm->tm_min, tm->tm_sec,
		     (unsigned int)(usec/10),
		     file, line
		);
	n += vsnprintf(buf+n, SOZ(sizeof(buf),n), format, valist1);
	n += snprintf(buf+n, SOZ(sizeof(buf),n), "\n");

	write_exact(STDERR_FILENO, buf, n);

	va_end(valist1);
}
