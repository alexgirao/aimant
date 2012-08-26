#ifndef nqces94afs4ka2k2fi /* str-h */
#define nqces94afs4ka2k2fi /* str-h */

#include <time.h>

#ifdef __cplusplus
extern "C" { /* assume C declarations for C++ */
#endif

#include <stdarg.h>

#ifdef _MSC_VER
#define __attribute__(x)
#endif

struct str {
  char *s;
  int len; /* can be changed between 0 and a-1 (inclusive) to truncate string */
  int a; /* allocated */
};

#define NULL_STR {NULL, 0, 0}
#define DEFINE_STR(sym) struct str sym[1] = {NULL_STR}

void str_alloc(struct str *x, int n);
void str_free(struct str *x);

void str_copyn(struct str *, const char *, int);
void str_copy(struct str *, const struct str *);
void str_copyz(struct str *, const char *);
void str_copyc(struct str *sa, int c);

void str_catn(struct str *, const char *, int);
void str_cat(struct str *, const struct str *);
void str_catz(struct str *, const char *);
void str_catc(struct str *sa, int c);

void str_vformat(struct str *sa, int cat, const char *fmt, va_list va);
void str_copyf(struct str *sa, const char *fmt, ...) __attribute__ ((format (printf, 2, 3)));
void str_catf(struct str *sa, const char *fmt, ...) __attribute__ ((format (printf, 2, 3)));

int str_diffn(struct str *a, char *b, int bl);
int str_diff(struct str *a, struct str *b);
int str_diffz(struct str *a, char *b);

void str_upper(struct str *s);
void str_lower(struct str *s);

int str_len(struct str *s);
int str_is_empty(struct str *s);

/*
 * str_shiftr:
 *   start .. end range: 0 .. ? (exclusive, -1 allowed for both (means: len - abs(end)))
 * str_shiftl:
 *   start .. end range: 0 .. len (exclusive, -1 allowed for both (means: len - abs(end)))
 * str_shiftr2:
 *   likewise, end is always len + n (always expand)
 * str_shiftl2:
 *   likewise, end is always len (shift from end of string)
 */

void str_shiftr(struct str *s, int start, int end, int n, int pad);
void str_shiftl(struct str *s, int start, int end, int n, int pad);
void str_shiftr2(struct str *s, int start, int n, int pad);
void str_shiftl2(struct str *s, int start, int n, int pad);

void str_from_file(struct str *s, const char *file);

void str_formattime(struct str *sa, int cat, const char *fmt, struct tm *tm);
void str_copyftime(struct str *sa, const char *fmt, struct tm *tm);
void str_catftime(struct str *sa, const char *fmt, struct tm *tm);

#ifdef __cplusplus
}; /* end of function prototypes */
#endif

#endif /* ! nqces94afs4ka2k2fi str-h */
