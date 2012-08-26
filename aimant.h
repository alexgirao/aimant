#ifndef nndkh2b7jr7nt4v1qe /* aimant-h */
#define nndkh2b7jr7nt4v1qe /* aimant-h */

struct sink {
	int fd; /* shortcut to sp->child_fdin */
	struct subprocess sp[1];
	int got_eof;
};

struct fd_tap {
	int fd;
	int bytes_read;
	//struct timeval time_read[1];
	int got_eof;
};

#if 1
struct file_tap {
	int fd;
	struct str path[1];
	int bytes_read;
	//struct timeval time_read[1];
	int got_eof;
};
#endif

struct cat_tap { /* "tail -fn0" really */
	int fd; /* shortcut to sp->child_fdout */
	struct str path[1];
	struct subprocess sp[1];
	int bytes_read;
	//struct timeval time_read[1];
	int got_eof;
};

#endif /* !nndkh2b7jr7nt4v1qe aimant-h */
