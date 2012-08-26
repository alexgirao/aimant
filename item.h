#ifndef npnta96oo91bqikdb8 /* item-h */
#define npnta96oo91bqikdb8 /* item-h */

#ifdef __cplusplus
extern "C" { /* assume C declarations for C++ */
#endif

/* item
 */

#define DEFINE_ITEM_HEADER(name, st_members)				\
	struct name {							\
		int _position;						\
		struct name *tail;					\
		st_members						\
	};								\
	struct name##_iterator {					\
		void *next, *end;					\
		void *v0;						\
		struct name **v1;					\
		int l0;							\
	};								\
	typedef struct name *(name##_next_func)(struct name##_iterator *i); \
	typedef void (name##_end_func)(struct name##_iterator *i);	\
	struct name *name##_tail0(struct name *x, struct name *tail);	\
	struct name *name##_new0(struct name *tail);	\
	void name##_free0(struct name *x);			\
	struct name **name##_as_array(struct name *x);	\
	int name##_len(struct name *x);				\
	void name##_backward(struct name##_iterator *i, struct name *x); \
	void name##_forward(struct name##_iterator *i, struct name *x);	\
	void name##_end(struct name##_iterator *i);			\
	struct name *name##_next(struct name##_iterator *i);		\
	struct name *name##_reverse(struct name *h);	\
	struct name *name##_foreach(struct name *x[2])

#define DEFINE_ITEM_IMPLEMENTATION(name)				\
	struct name *name##_tail0(struct name *x, struct name *tail) { \
		x->_position = tail ? tail->_position + 1 : 0;		\
		x->tail = tail;						\
		return x;						\
	}								\
	struct name *name##_new0(struct name *tail) {	\
		struct name *r;					\
		r = (struct name*)calloc(1, sizeof(struct name)); \
		r = name##_tail0(r, tail);				\
		return r;						\
	}								\
	void name##_free0(struct name *x) {			\
		while (x) {						\
			struct name *t = x->tail;			\
			free(x);					\
			x = t;						\
		}							\
	}								\
	struct name **name##_as_array(struct name *x) {	\
		struct name *t, **r;				\
		if (!x) return NULL;					\
		r = (struct name**)calloc(x->_position + 1, sizeof(struct name*)); \
		for (t=x; t; t=t->tail) {				\
			r[t->_position] = t;				\
		}							\
		return r;						\
	}								\
	int name##_len(struct name *x) {				\
		return x ? x->_position + 1 : 0;			\
	}								\
	struct name *name##_index(struct name *x, int index) { \
		if (x && index >= 0 && index <= x->_position) {		\
			do {						\
				if (index == x->_position) return x;	\
			} while ((x = x->tail));			\
		}							\
		return NULL;						\
	}								\
	static inline struct name *_##name##_next_b(struct name##_iterator *i) {	\
		struct name *r = (struct name*)i->v0;	\
		if (r == NULL) {					\
			i->next = NULL;					\
			return NULL;					\
		}							\
		i->v0 = r->tail;					\
		return r;						\
	}								\
	void name##_backward(struct name##_iterator *i, struct name *x) { \
		if (!x || !i) return;					\
		i->next = (void*) _##name##_next_b;			\
		i->end = NULL;						\
		i->v0 = x;						\
	}								\
	static inline void _##name##_end_f(struct name##_iterator *i) {	\
		i->next = NULL;						\
		i->end = NULL;						\
		i->v1 = NULL;						\
		free(i->v0);						\
		i->v0 = NULL;						\
	}								\
	static inline struct name *_##name##_next_f(struct name##_iterator *i) {	\
		if (i->l0) {						\
			i->l0--;					\
			return *i->v1++;				\
		}							\
		_##name##_end_f(i);					\
		return NULL;						\
	}								\
	void name##_forward(struct name##_iterator *i, struct name *x) {	\
		if (!x || !i) return;					\
		i->next = (void*)_##name##_next_f;			\
		i->end = (void*)_##name##_end_f;			\
		i->v0 = name##_as_array(x);				\
		i->v1 = (struct name**)i->v0;			\
		i->l0 = x->_position + 1;				\
	}								\
	struct name *name##_next(struct name##_iterator *i) {		\
		if (i->next) {						\
			return ((name##_next_func*)i->next)(i);		\
		}							\
		return NULL;						\
	}								\
	void name##_end(struct name##_iterator *i) {				\
		if (i->end) {						\
			((name##_end_func*)i->end)(i);			\
		}							\
	}								\
	static inline struct name *name##_reverse0(struct name *parent, struct name *h, int pos) { \
		if (h->tail) {						\
			struct name *r;				\
			r = name##_reverse0(h, h->tail, pos + 1);	\
			h->_position = pos;				\
			h->tail = parent;				\
			return r;					\
		}							\
		h->_position = pos;					\
		h->tail = parent;					\
		return h;						\
	}								\
	struct name *name##_reverse(struct name *h) {	\
		if (!h) return NULL;					\
		return name##_reverse0(NULL, h, 0);			\
	}								\
	struct name *name##_foreach(struct name *x[2]) {	\
		if (x[1]) {						\
			x[0] = x[1];					\
			x[1] = x[0]->tail;				\
			return x[0];					\
		}							\
		return NULL;						\
	}

#define DEFINE_ITEM(name, st_members)           \
	DEFINE_ITEM_HEADER(name, st_members);	\
	DEFINE_ITEM_IMPLEMENTATION(name)

/* queue
 */

#define DEFINE_QUEUE_HEADER(name, item)				\
	struct name {						\
		struct item *enqueue;				\
		struct item *dequeue;				\
	};							\
	struct name *name##_new0();				\
	void name##_free0(struct name *x);			\
	int name##_len(struct name *x);				\
	struct item *name##_dequeue(struct name *q)

#define DEFINE_QUEUE_IMPLEMENTATION(name, item)				\
	struct name *name##_new0() {				\
		return (struct name*)calloc(1, sizeof(struct name)); \
	}								\
	void name##_free0(struct name *x) {			\
		if (x) {						\
			item##_free0(x->enqueue);			\
			item##_free0(x->dequeue);			\
			free(x);					\
		}							\
	}								\
	int name##_len(struct name *x) {				\
		return x ?						\
			item##_len(x->enqueue) +			\
			item##_len(x->dequeue) : 0;			\
	}								\
	struct item *name##_dequeue(struct name *q) {			\
		struct item *r;						\
		if (q->dequeue == NULL) {				\
			if (q->enqueue) {				\
				q->dequeue = item##_reverse(q->enqueue); \
				q->enqueue = NULL;			\
			} else {					\
				return NULL;				\
			}						\
		}							\
		r = q->dequeue;						\
		q->dequeue = r->tail;					\
		r->tail = NULL;						\
		return r;						\
	}

#define DEFINE_QUEUE(name, item)                \
	DEFINE_QUEUE_HEADER(name, item);	\
	DEFINE_QUEUE_IMPLEMENTATION(name, item)

#ifdef __cplusplus
}; /* end of function prototypes */
#endif

#endif
