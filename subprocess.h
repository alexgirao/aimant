#ifndef nj62s3cfembue1c6om /* subprocess-h */
#define nj62s3cfembue1c6om

struct subprocess
{
	char **argv;
	char **envp;
	int search_path; /* set to non-zero to use execvp instead of execv */
	int exit_status; /* man 2 waitpid */
	int pid;
	int waitpid_pid;
	void *attachment; /* plug any data you want here, it will be available for all callbacks */
	int child_fdin;
	int child_fdout;
	int child_fderr;
	int is_gone; /* self-pipe trick */
};

struct subprocess_callbacks
{
	int ctimeout; /* consume timeout in milliseconds */
	int ptimeout; /* produce timeout in milliseconds */
	int produce_immediately; /* subprocess receives data first */
	void (*consume_stdout)(struct subprocess *sp, void *data, int sz);
	void (*consume_stderr)(struct subprocess *sp, void *data, int sz);
	int (*produce_stdin)(struct subprocess *sp, int fdin); /* return non-zero to check stdin availability immediatly */
	int (*consume_timeout)(struct subprocess *sp, int fdin, int fdin_ready); /* return non-zero to terminate child */
	int (*produce_timeout)(struct subprocess *sp); /* return non-zero to terminate child */
};

#define ST_SUBPROCESS(v) struct subprocess v[1] = {{NULL, NULL, 0, 0, 0, 0, NULL, 0, 0, 0, 0}};
#define ST_SUBPROCESS_CALLBACKS(v) struct subprocess_callbacks v[1] = {{0, 0, 0, NULL, NULL, NULL, NULL, NULL}};

void interrupt_safe_sleep(int ms);
void get_current_timeval(struct timeval *tv);
int make_fd_non_blocking(int fd);

/* use callbacks and only return when subprocess terminates
 */
int subprocess_run(struct subprocess *sp, struct subprocess_callbacks *cb);

void subprocess_close_child_fdin(struct subprocess *sp);
void subprocess_close_child_fdout(struct subprocess *sp);
void subprocess_close_child_fderr(struct subprocess *sp);

/* same fork semantics
 */
int subprocess_fork0(struct subprocess *sp);

/* 0 on success, -1 on error, calls execve
 */
int subprocess_fork(struct subprocess *sp);

int subprocess_terminate(struct subprocess *sp);
void subprocess_exit_debug(struct subprocess *sp);

/* returns a fd on success (just check for readiness on it) or -1 on error
 */
int subprocess_get_selfpipe_read_fd();

/* returns 0 on success
 */
int subprocess_read_selfpipe();

/* returns 0 on timeout or returns the pid of child gone
 */
int subprocess_wait(struct subprocess *sp, int msec);

#endif /* !subprocess-h */
