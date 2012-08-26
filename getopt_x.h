
#include <ctype.h>

#define OPTSTRING_BUFFER_SIZE 100

struct getopt_x /* getopt_x info/state */
{
	/* prepare input
	 */
	int argc;
	char **argv;
	const char *optstring0;
	struct option *longopts;
	const char *optreq;

	/* calculated optstring
	 */
	char optstring[OPTSTRING_BUFFER_SIZE];

	/* auxiliary
	 */
	fd_set optset[1];  /* used as a boolean map to options */
	int verbose;

	/* used by getopt_x_next (only mutable data after prepare)
	 */
	int got_error;
	int got_dashdash;

	/* used by getopt_x_option
	 */
	char optstring0_alnum[OPTSTRING_BUFFER_SIZE];
	int n_short_options;
	int n_all_options;

	/* used by getopt_x_option_format
	 */
	int longest_name_option;
	char fmt_long[40];
	char fmt_both[40];
	char fmt_short[40];
};

#define BYTE_PLANE_0(v) ((v)&0xff)
#define BYTE_PLANE_1(v) (BYTE_PLANE_0(v) + 0x100)
#define BYTE_PLANE_2(v) (BYTE_PLANE_0(v) + 0x200)
#define BYTE_PLANE_3(v) (BYTE_PLANE_0(v) + 0x300)

#define OPTSET_IS_OPT_PLANE(v) BYTE_PLANE_0(v)
#define OPTSET_HAS_ARG_PLANE(v) BYTE_PLANE_1(v)
#define OPTSET_IS_REQUIRED_PLANE(v) BYTE_PLANE_2(v)
#define OPTSET_OPT_INFORMED_PLANE(v) BYTE_PLANE_3(v)

#define FD_SET_IS_OPT(v,s) FD_SET(OPTSET_IS_OPT_PLANE(v), s)
#define FD_ISSET_IS_OPT(v,s) FD_ISSET(OPTSET_IS_OPT_PLANE(v), s)

#define FD_SET_HAS_ARG(v,s) FD_SET(OPTSET_HAS_ARG_PLANE(v), s)
#define FD_ISSET_HAS_ARG(v,s) FD_ISSET(OPTSET_HAS_ARG_PLANE(v), s)

#define FD_SET_IS_REQUIRED(v,s) FD_SET(OPTSET_IS_REQUIRED_PLANE(v), s)
#define FD_ISSET_IS_REQUIRED(v,s) FD_ISSET(OPTSET_IS_REQUIRED_PLANE(v), s)

#define FD_SET_OPT_INFORMED(v,s) FD_SET(OPTSET_OPT_INFORMED_PLANE(v), s)
#define FD_ISSET_OPT_INFORMED(v,s) FD_ISSET(OPTSET_OPT_INFORMED_PLANE(v), s)

#define GETOPT_X_OPTION_IS_INFORMED(s,o) FD_ISSET_OPT_INFORMED(o,s->optset)

int getopt_x_prepare(struct getopt_x *state, int argc, char **argv, const char *optstring, struct option *longopts, const char *optreq);
int getopt_x_prepare_verbose(struct getopt_x *state, int argc, char **argv, const char *optstring, struct option *longopts, const char *optreq);
int getopt_x_next(struct getopt_x *state, struct option **opt);

int getopt_x_option(struct getopt_x *state, int c, struct option *opt);
int getopt_x_option_format(char *buf, int bufsz, struct getopt_x *state, struct option *opt);
void getopt_x_option_debug(struct getopt_x *state, int c, struct option *opt);

#define strncpy_safe(d,s,n)					\
	do {							\
		char *__d=d;					\
		const char *__s=s;				\
		int __n=n;					\
		assert(__n > 0);				\
		__d[__n-1]='Z'; /* this enforces spec */	\
		strncpy(__d,__s,__n);				\
		assert(__d[__n-1]==0);				\
	} while (0)

#define strncpy_sizeof(d,s) strncpy_safe(d,s,sizeof(d))
