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
 * global variables (man 3 getopt)
 *
 *   char *optarg;
 *   int optind, opterr, optopt;
 *
 * - optarg is the option value
 * - optopt is set in case of error
 *
 * note: optional arguments ('::' in optstring or .has_arg=2 in struct
 * option) are not allowed due to inconsistencies (e.g.: '-a0' versus
 * '-a 0' and '--b=1' versus '--b 1'), to use default values do it in
 * logic
 *
 * BUG: '-i-a' vs '-i -a' (-a get interpreted as -i! should give an
 * error (unknown optopt '-'))
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

#include "debug0.h"

#include "bsd-getopt_long.h"
#include "getopt_x.h"

/*#define OPTSET_VERIFY*/

#ifdef OPTSET_VERIFY
void optset_verify(fd_set *optset)
{
	int i;
	DEBUG("optset verification");
	for (i=0; i<256; i++) {
		int is_opt = FD_ISSET_IS_OPT(i, optset);
		int has_arg = FD_ISSET_HAS_ARG(i, optset);
		int is_required = FD_ISSET_IS_REQUIRED(i, optset);
		int opt_informed = FD_ISSET_OPT_INFORMED(i, optset);
		if (is_opt) {
			DEBUG("\x20\x20option '%c' (byte %i): has_arg? %i, is_required? %i, opt_informed? %i",
			      i, i, has_arg, is_required, opt_informed);
		} else {
			assert(!has_arg && !is_required && !opt_informed);
		}
	}
}
#endif

static int getopt_x_prepare0(struct getopt_x *state, int argc, char **argv, const char *optstring, struct option *longopts, const char *optreq, int verbose)
{
	int optstring_len = 0;
	int optstring0_alnum_len = 0;

	opterr = 0; /* prevent the error message */

#ifdef GETOPTX_CHECK_FUNDAMENTALS
	/* fundamental assumptions
	 */

	assert(required_argument == 1);
	assert(FD_SETSIZE == 1024);
	assert(BYTE_PLANE_0(0) == 0);
	assert(BYTE_PLANE_0(255) == 255);
	assert(BYTE_PLANE_1(0) == 256);
	assert(BYTE_PLANE_1(255) == 256+255);
	assert(BYTE_PLANE_2(0) == 512);
	assert(BYTE_PLANE_2(255) == 512+255);
	assert(BYTE_PLANE_3(0) == 768);
	assert(BYTE_PLANE_3(255) == 768+255);
	assert(BYTE_PLANE_3(255) == FD_SETSIZE-1);
#endif

	/* sanity check
	 */

	assert(longopts);

	/* initialize
	 */

	memset(state, 0, sizeof(struct getopt_x));
	/* FD_ZERO(state->optset); */

	state->argc = argc;
	state->argv = argv;
	state->optstring0 = optstring;
	state->longopts = longopts;

	state->optreq = optreq;

	state->verbose = verbose;

	/* optstring
	 */

	if (verbose) {
		DEBUG("optstring=[%s]", optstring);
		DEBUG("optreq=[%s]", optreq);
	}

	assert(optstring_len < sizeof(state->optstring)-1);
	state->optstring[optstring_len++] = '-'; /* this turn non-option arguments swap off */

	assert(optstring_len < sizeof(state->optstring)-1);
	state->optstring[optstring_len++] = ':'; /* this diferentiates error from unknown option */

	if (optstring && *optstring) {
		int prior = 0;
		const char *cp = optstring;
		assert(isalnum(*cp));
		do {
			if (isalnum(*cp)) {
				if (FD_ISSET_IS_OPT(*cp, state->optset)) {
					DEBUG("error: duplicate option '%c' in optstring", *cp);
					return -1;
				}
				FD_SET_IS_OPT(*cp, state->optset);
				if (prior && prior != ':') {
					if (verbose) DEBUG("found short option -%c, has arg? 0", prior);
					state->n_short_options++;
				}

				assert(optstring0_alnum_len < sizeof(state->optstring0_alnum)-1);
				state->optstring0_alnum[optstring0_alnum_len++] = *cp;
			} else if (*cp == ':') {
				if (prior == ':') {
					/* optional arguments not allowed here, use
					 * ':' in return of getopt
					 */
					DEBUG("error: optional arguments not allowed in optstring");
					return -1;
				} else {
					if (verbose) DEBUG("found short option -%c, has arg? 1", prior);
					FD_SET_HAS_ARG(prior, state->optset);
					state->n_short_options++;
				}
			} else {
				if (isprint(*cp)) {
					DEBUG("error: invalid char '%c' (byte %i) in optstring", *cp, *cp);
				} else {
					DEBUG("error: invalid byte %i in optstring", *cp);
				}
				return -1;
			}

			assert(optstring_len < sizeof(state->optstring)-1);
			state->optstring[optstring_len++] = *cp;

			prior = *cp;
		} while (*++cp);

		if (verbose) {
			if (prior != ':') {
				DEBUG("found short option -%c, has arg? 0", prior);
				state->n_short_options++;
			}
		}
	}

	/* close state->optstring0_alnum
	 */

	state->optstring0_alnum[optstring0_alnum_len++] = 0;

	/* DEBUG("state->optstring0_alnum=[%s]", state->optstring0_alnum);
	 */

	/* state->longopts
	 */

	state->n_all_options = state->n_short_options;

	if (state->longopts->name) {
		struct option *opt = state->longopts;
		do {
			int namelen;
			if ((namelen = strlen(opt->name)) > state->longest_name_option) {
				state->longest_name_option = namelen;
			}
			if (opt->val) {
				assert(isalnum(opt->val));
				if (verbose) DEBUG("found long option -%c/--%s, has arg? %i", opt->val, opt->name, !!opt->has_arg);
				if (FD_ISSET_IS_OPT(opt->val, state->optset)) {
					DEBUG("error: duplicate option -%c/--%s in longopts", opt->val, opt->name);
					return -1;
				}
				FD_SET_IS_OPT(opt->val, state->optset);
				assert(optstring_len < sizeof(state->optstring)-1);
				state->optstring[optstring_len++] = opt->val;
				if (opt->has_arg) {
					assert(opt->has_arg == 1); /* optional arguments not allowed */
					assert(optstring_len < sizeof(state->optstring)-1);
					state->optstring[optstring_len++] = ':';
					FD_SET_HAS_ARG(opt->val, state->optset);
				}
			} else {
				if (opt->has_arg) {
					assert(opt->has_arg == 1); /* optional arguments not allowed */
				}
				if (verbose) DEBUG("found long option --%s, has arg? %i", opt->name, !!opt->has_arg);
			}
			state->n_all_options++;
			opt++;
		} while (opt->name);
	}

	/* close state->optstring
	 */

	state->optstring[optstring_len++] = 0;

	/* optreq
	 */

	if (optreq && *optreq) {
		const char *cp = optreq;
		do {
			if (isalnum(*cp)) {
				if (!FD_ISSET_IS_OPT(*cp, state->optset)) {
					DEBUG("error: unknown option '%c' in optstring", *cp);
					return -1;
				}
				if (FD_ISSET_IS_REQUIRED(*cp, state->optset)) {
					DEBUG("error: duplicate option '%c' in optreq", *cp);
					return -1;
				}
				FD_SET_IS_REQUIRED(*cp, state->optset);
			} else {
				if (isprint(*cp)) {
					DEBUG("error: invalid char '%c' (byte %i) in optreq", *cp, *cp);
				} else {
					DEBUG("error: invalid byte %i in optreq", *cp);
				}
				return -1;
			}
		} while (*++cp);
	}

	{
		int i = state->longest_name_option + 4;
		snprintf(state->fmt_both, sizeof(state->fmt_both), ", --%%-%is", i + 6);
		snprintf(state->fmt_long, sizeof(state->fmt_long), "--%%-%is", i + 8);
		snprintf(state->fmt_short, sizeof(state->fmt_short), " %%-%is", i + 9);
	}

	/*
	 */

	if (verbose) DEBUG("state->optstring=[%s]", state->optstring);

#ifdef OPTSET_VERIFY
	optset_verify(state->optset);
#endif
	return 0;
}

int getopt_x_prepare(struct getopt_x *state, int argc, char **argv, const char *optstring, struct option *longopts, const char *optreq)
{
	return getopt_x_prepare0(state, argc, argv, optstring, longopts, optreq, 0 /* verbose */);
}

int getopt_x_prepare_verbose(struct getopt_x *state, int argc, char **argv, const char *optstring, struct option *longopts, const char *optreq)
{
	return getopt_x_prepare0(state, argc, argv, optstring, longopts, optreq, 1 /* verbose */);
}

int getopt_x_next(struct getopt_x *state, struct option **opt)
{
	int c;
	int index;

	if (state->got_error) {
		return -1;
	}

	if (state->got_dashdash) {
		if (optind == state->argc) return -1;
		optarg = state->argv[optind++];
		return 1;
	}

	index = -1;

	if ((c = getopt_long(state->argc, state->argv, state->optstring, state->longopts, &index)) == -1) {
		if (optind < state->argc) {
			assert(strcmp(state->argv[optind - 1], "--") == 0);
			state->got_dashdash = 1;
			optarg = state->argv[optind++];
			return 1;
		}
		if (state->optreq && *state->optreq) {
			const char *cp = state->optreq;
			do {
				if (!FD_ISSET_OPT_INFORMED(*cp, state->optset)) {
					DEBUG("error: option -%c is required", *cp);
					state->got_error = -1;
				}
			} while (*++cp);
		}
		return -1;
	}

	//DEBUG("c=%c/%i, index=%i, optopt=%c/%i", c, c, *indexp, optopt, optopt);

	/* sanity check
	 */

	switch (c) {
	case ':':
		if (optopt) {
			if (FD_ISSET_HAS_ARG(optopt, state->optset)) {
				DEBUG("error: option -%c need an argument", optopt);
			} else {
				DEBUG("error: option -%c does not accept an argument", optopt);
			}
		} else {
			DEBUG("error: invalid argument [%s], index=%i", state->argv[optind - 1], index);
		}
		state->got_error = 1;
		return -1;
	case '?': break; /* unknown */
	case 0: break; /* long option only */
	default:
		FD_SET_OPT_INFORMED(c, state->optset);
	}

	if (opt) {
		if (index >= 0) {
			*opt = &state->longopts[index];
		} else {
			*opt = NULL;
		}
	}

	return c;
}

int getopt_x_option(struct getopt_x *state, int c, struct option *opt)
{
	assert(opt);
	if (c < 0) {
		return -1;
	} else if (c < state->n_short_options) {
		opt->name = NULL;
		opt->val = state->optstring0_alnum[c];
		opt->has_arg = FD_ISSET_HAS_ARG(opt->val, state->optset);
	} else if (c < state->n_all_options) {
		*opt = state->longopts[c - state->n_short_options];
	} else {
		return -1;
	}
	return c + 1;
}

/* sub or zero */
#define SOZ(a,b) ((a) > (b) ? (a) - (b) : 0)

int getopt_x_option_format(char *buf, int bufsz, struct getopt_x *state, struct option *opt)
{
	int pos = 0;
	pos += snprintf(buf + pos, SOZ(bufsz,pos), "  ");
	if (opt && opt->val) {
		assert(isalnum(opt->val));
		pos += snprintf(buf + pos, SOZ(bufsz,pos), " -%c", opt->val);
	} else {
		pos += snprintf(buf + pos, SOZ(bufsz,pos), "   ");
	}
	if (opt && opt->name) {
		char tmp[256];
		const char *p;
		if (opt->has_arg) {
			snprintf(tmp, sizeof(tmp), "%s <arg>", opt->name ? opt->name : "");
			p = tmp;
		} else {
			p = opt->name;
		}
		if (opt->val) {
			pos += snprintf(buf + pos, SOZ(bufsz,pos), state->fmt_both, p);
		} else {
			pos += snprintf(buf + pos, SOZ(bufsz,pos), state->fmt_long, p);
		}
		if (pos >= bufsz) {
			/* we can't signalize as error since
			 * getopt_x_option_format follows the same
			 * snprintf semantics
			 */
			DEBUG("warning: buffer truncated");
		}
	} else {
		pos += snprintf(buf + pos, SOZ(bufsz,pos), state->fmt_short, opt && opt->has_arg ? "<arg>" : "");
	}
	return pos;
}

void getopt_x_option_debug(struct getopt_x *state, int c, struct option *opt)
{
	char **argv = state->argv;
	switch (c) {
	case 0:
		assert(opt);
		DEBUG("long option [%s] with value [%s]", opt->name, optarg);
		break;
	case 1:
		DEBUG("non-option argument [%s]", optarg);
		break;
	case '?':
		if (isprint(optopt)) DEBUG("unknown optopt '%c' byte %i", optopt, optopt);
		else DEBUG("unknown option [%s] (argv[%i])", argv[optind - 1], optind - 1);
		break;
	case -1:
		DEBUG("error: incorrect usage, not handling option -1, end of options");
		abort();
		break;
	default:
		assert(c > 1);
		if (isprint(c)) DEBUG("unhandled option '%c' byte %i with value [%s]", c, c, optarg);
		else DEBUG("exhaustion [%s] (argv[%i])", argv[optind - 1], optind - 1);
	}
}
