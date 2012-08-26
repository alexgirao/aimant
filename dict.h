#ifndef npt0e4bpidtjo6n4l5 /* dict-h */
#define npt0e4bpidtjo6n4l5 /* dict-h */

#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#define RB_COMPACT // embed color bits in right-child pointers.
#include "rb.h"

/* Root structure. */
#define	rb_tree2(dict, a_type)			\
	struct dict {				\
		a_type *rbt_root;		\
		a_type rbt_nil;			\
	}

#define DEFINE_DICT(dict, item, st_members)				\
	struct item {							\
		rb_node(struct item) _meta;				\
		st_members;						\
	};								\
	int item##_cmp(struct item *a, struct item *b);			\
	rb_tree2(dict, struct item);					\
	rb_gen(, dict##_, struct dict, struct item, _meta, item##_cmp);	\
	struct item *item##_new0() {					\
		struct item *r;						\
		r = (struct item *)calloc(1, sizeof(struct item));	\
		return r;						\
	}								\
	void item##_free0(struct item *x) {				\
		if (x) free(x);						\
	}								\
	struct dict *dict##_new0() {					\
		struct dict *r;						\
		r = (struct dict *)calloc(1, sizeof(struct dict));	\
		dict##_new(r);						\
		return r;						\
	}								\
	void dict##_free0(struct dict *x) {				\
		if (x) {						\
			struct item *c, *n;				\
			for (c = dict##_first(x); c; c = n) {		\
				n = dict##_next(x, c);			\
				dict##_remove(x, c);			\
				item##_free0(c);			\
			}						\
			free(x);					\
		}							\
	}

#endif /* ! npt0e4bpidtjo6n4l5 dict-h */
