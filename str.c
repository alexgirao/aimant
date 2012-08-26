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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "str.h"

#define FATAL(msg) do {fprintf(stderr, "fatal:%s:%i:%s\n", __FILE__, __LINE__, msg); exit(1);} while (0)

static void *realloc_(void *x, int m, int n)
{
  void *y = malloc(n);
  if (!y) return NULL;
  memcpy(y, x, m);
  free(x);
  return y;
}

static int bdiff(const void *s, int n, const void *t)
{
  const char *x=s;
  const char *y=t;
  for (;;) {
    if (!n)
      return 0;
    if (*x != *y)
      break;
    ++x;
    ++y;
    --n;
  }
  return ((int)(unsigned char)*x)
    - ((int)(unsigned char)*y);
}

static void bcopyl(void *to, int n, const void *from)
{
  char *t = to;
  const char *f = from;
  for (;;) {
    if (!n)
      return;
    *t++ = *f++;
    --n;
  }
}

static void bcopyr(void *to, int n, const void *from)
{
  char *t = (char*)to + n;
  const char *f = (char*)from + n;
  for (;;) {
    if (!n)
      return;
    *--t = *--f;
    --n;
  }
}

static int sdiffn(const char *s, const char *t, int len)
{
  char x;

  for (;;) {
    if (!len--)
      return 0;
    x = *s;
    if (x != *t)
      break;
    if (!x)
      break;
    ++s;
    ++t;
  }

  return ((int)(unsigned char)x)
    - ((int)(unsigned char)*t);
}

/* alloc/free
 */

void str_alloc(struct str *x, int n)
{
  if (x->s) {
    if (n > x->a) {
      int i = n + (n >> 3) + 30;
      void *p = realloc_(x->s, x->len, i);
      if (p) {
        x->a = i;
        x->s = p;
        return;
      }
      FATAL("memory allocation failed"); /* it's better be safer than sorry */
    }
    return;
  }
  x->s = malloc(n);
  assert(x->s);
  x->a = n;
  x->len = 0;
}

void str_free(struct str *x)
{
  if (x->s) {
    void *p = x->s;
    x->s = NULL;
    x->len = 0;
    x->a = 0;
    free(p);
    p = NULL;
  }
}

/* copy
 */

void str_copyn(struct str *sa, const char *s, int n)
{
  str_alloc(sa, n + 1);
  memcpy(sa->s, s, n);
  sa->len = n;
  sa->s[n] = 0;
}

void str_copy(struct str *to, const struct str *from)
{
  str_copyn(to, from->s, from->len);
}

void str_copyz(struct str *sa, const char *s)
{
  str_copyn(sa, s, strlen(s));
}

void str_copyc(struct str *sa, int c)
{
  if (sa->len == 0) {
    str_alloc(sa, 1);
  }
  sa->s[0] = c;
  sa->len = 1;
}

/* cat
 */

void str_catn(struct str *sa, const char *s, int n)
{
  if (!sa->s) {
    str_copyn(sa, s, n);
    return;
  }
  str_alloc(sa, sa->len + n + 1);
  memcpy(sa->s + sa->len, s, n);
  sa->len += n;
  sa->s[sa->len] = 0;
}

void str_cat(struct str *to, const struct str *from)
{
  str_catn(to, from->s, from->len);
}

void str_catz(struct str *sa, const char *s)
{
  str_catn(sa, s, strlen(s));
}

void str_catc(struct str *sa, int c)
{
  if ((sa->len + 1) >= sa->a) {
    str_alloc(sa, sa->len + 2);
  }
  sa->s[sa->len++] = c;
  sa->s[sa->len] = 0;
}

/* format
 */

void str_vformat(struct str *sa, int cat, const char *fmt, va_list va)
{
#if 0 /* old implementation */
  char buf0[0x1fff + 1 /* 8192 */], *buf;
  int buf_len;
  int n;

  buf = buf0;
  buf_len = sizeof(buf0);

  assert((n = vsnprintf(buf, buf_len, fmt, va)) >= 0);
  if (n >= buf_len) {
    /* truncated, try with more space
     */
    buf_len = 0xffff + 1/* 64K */;
    assert((buf = malloc(buf_len)) != NULL);
    assert((n = vsnprintf(buf, buf_len, fmt, va)) >= 0);
    if (n >= buf_len) {
      free(buf);
      buf_len = 0xfffff + 1 /* 1M */;
      assert((buf = malloc(buf_len)) != NULL);
      assert((n = vsnprintf(buf, buf_len, fmt, va)) >= 0);
      if (n >= buf_len) {
        free(buf);
        buf_len = 0xffffff + 1; /* 16M ! */
        assert((buf = malloc(buf_len)) != NULL);
        assert((n = vsnprintf(buf, buf_len, fmt, va)) >= 0);
        if (n >= buf_len) {
          /* give up */
          free(buf);
          fprintf(stderr, "error: str_vformat(): too large input (> %i)\n", 0xffffff + 1);
          exit(1);
        }
      }
    }
  }

  assert(buf != NULL);

  if (cat) {
    str_catn(sa, buf, n);
  } else {
    str_copyn(sa, buf, n);
  }

  if (buf && buf != buf0) {
    free(buf);
  }
#else
  int n;
  va_list va2;

  va_copy(va2, va);

  assert(sa->s == NULL || sa->len <= sa->a);

  cat = cat ? sa->len : 0;

  /* get needed size, n does not include the trailing '\0'
   */
  if (sa->s) {
    n = vsnprintf(sa->s + cat, sa->a - cat, fmt, va);
  } else {
    n = vsnprintf(NULL, 0, fmt, va); /* this behaviour requires a C99 standard */
  }
  if (n < 0) {
    /* error */
    perror("vsnprintf()");
    exit(1);
  }

  if (sa->s && (cat + n) < sa->a) {
    /* string is allocated and required space (cat + n) is lesser than
     * allocated buffer, meaning that all non-zero bytes plus the null
     * terminator ('\0') fits in sa->a
     */
    sa->len = cat + n;
  } else {
    /* no buffer or insufficient buffer size
     */
    str_alloc(sa, cat + n + 1); /* +1 for null terminator, snprintf requires it */
    sa->len = cat + vsnprintf(sa->s + cat, sa->a - cat, fmt, va2);
  }

  va_end(va2);
#endif
}

/* format time (strftime)
 */

void str_formattime(struct str *sa, int cat, const char *fmt, struct tm *tm)
{
  char buf0[0x0fff + 1 /* 4096 */], *buf;
  int buf_len;
  int n;

  buf = buf0;
  buf_len = sizeof(buf0);

  n = strftime(buf, buf_len, fmt, tm);
  if (n == 0 || n >= buf_len) {
    /* try with more space
     */
    buf_len = 0x1fff + 1 /* 8192 */;
    assert((buf = malloc(buf_len)) != NULL);
    n = strftime(buf, buf_len, fmt, tm);
    if (n == 0 || n >= buf_len) {
      free(buf);
      buf_len = 0xffff + 1 /* 64K */;
      assert((buf = malloc(buf_len)) != NULL);
      n = strftime(buf, buf_len, fmt, tm);
      if (n == 0 || n >= buf_len) {
        free(buf);
        buf_len = 0xfffff + 1; /* 1M */
        assert((buf = malloc(buf_len)) != NULL);
        n = strftime(buf, buf_len, fmt, tm);
        if (n == 0 || n >= buf_len) {
          /* give up */
          free(buf);
          fprintf(stderr, "error: str_vformattime(): too large input (> %i)\n", 0xfffff + 1);
          exit(1);
        }
      }
    }
  }

  assert(buf != NULL);

  if (cat) {
    str_catn(sa, buf, n);
  } else {
    str_copyn(sa, buf, n);
  }

  if (buf && buf != buf0) {
    free(buf);
  }
}

void str_copyf(struct str *sa, const char *fmt, ...)
{
  va_list va;
  va_start(va, fmt);
  str_vformat(sa, 0, fmt, va);
  va_end(va);
}

void str_catf(struct str *sa, const char *fmt, ...)
{
  va_list va;
  va_start(va, fmt);
  str_vformat(sa, 1, fmt, va);
  va_end(va);
}

void str_copyftime(struct str *sa, const char *fmt, struct tm *tm)
{
  str_formattime(sa, 0, fmt, tm);
}

void str_catftime(struct str *sa, const char *fmt, struct tm *tm)
{
  str_formattime(sa, 1, fmt, tm);
}

/* diff
 */

int str_diffn(struct str *a, char *b, int bl)
{
  int x = a->len - bl;
  int y = 0;

  if (x > 0) {
    x = 1;
    y = bdiff(a->s, bl, b);
  } else if (x < 0) {
    x = -1;
    y = bdiff(a->s, a->len, b);
  } else {
    y = bdiff(a->s, a->len, b);
  }
  return y ? y : x;
}

int str_diffz(struct str *a, char *b)
{
  str_catn(a, "\0", 1);
  return sdiffn(a->s, b, a->len--);
}

int str_diff(struct str *a, struct str *b)
{
  return str_diffn(a, b->s, b->len);
}

/* case change
 */

void str_upper(struct str *s)
{
  int i;
  char c;
  for (i=0; i<s->len; i++, c++) {
    c = s->s[i];
    if (isalpha(c)) {
      s->s[i] = toupper(c);
    }
  }
}

void str_lower(struct str *s)
{
  int i;
  char c;
  for (i=0; i<s->len; i++, c++) {
    c = s->s[i];
    if (isalpha(c)) {
      s->s[i] = tolower(c);
    }
  }
}

/* shift
 */

void str_shiftr(struct str *s, int start, int end, int n, int pad)
{
  int i, window_size;
  char *ss;

  if (start < 0) {
    start = s->len + start;
  }

  if (end < 0) {
    end = s->len + end;
  }

  assert(n >= 0);
  assert(start >= 0);
  assert(start <= s->len);

  window_size = end - start;

  assert(end >= 0);
  assert(start <= end);
  assert(n <= window_size);

  if (end > s->len) {
    /* end is exclusive, so its len compatible
     */
    str_alloc(s, end);
    s->len = end;
  }
  ss = s->s + start;
  bcopyr(ss + n, window_size - n, ss);

  for (i=0; i<n; i++) {
    ss[i] = pad;
  }
}

void str_shiftl(struct str *s, int start, int end, int n, int pad)
{
  int i, window_size;
  char *ss;

  if (start < 0) {
    start = s->len + start;
  }

  if (end < 0) {
    end = s->len + end;
  }

  assert(n >= 0);
  assert(start >= 0);
  assert(start <= s->len);

  window_size = end - start;

  assert(end >= 0);
  assert(start <= end);
  assert(n <= window_size);

  assert(end <= s->len);

  ss = s->s + start;
  bcopyl(ss, window_size - n, ss + n);

  ss += window_size - n;
  for (i=0; i<n; i++) {
    ss[i] = pad;
  }
}

void str_shiftr2(struct str *s, int start, int n, int pad)
{
  str_shiftr(s, start, s->len + n, n, pad);
}

void str_shiftl2(struct str *s, int start, int n, int pad)
{
  str_shiftl(s, start, s->len, n, pad);
}

static off_t get_file_size(const char *f)
{
  struct stat st[1];
  assert(stat(f, st) == 0);
  return st->st_size;
}

static void *get_file_data(const char *fname, off_t fsz, void *buf)
{
  FILE *f = fopen(fname, "rb");
  assert(buf);
  assert(f);
  assert(fread(buf, fsz, 1, f) == 1);
  fclose(f);
  return buf;
}

void str_from_file(struct str *s, const char *file)
{
  off_t fsz = get_file_size(file);
  str_alloc(s, fsz + 1);
  get_file_data(file, fsz, s->s);
  s->s[fsz] = 0;
  s->len = fsz;
}

int str_len(struct str *x)
{
  return x->s ? x->len : 0;
}

int str_is_empty(struct str *x)
{
  return str_len(x) == 0;
}
