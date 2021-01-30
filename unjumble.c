#define _XOPEN_SOURCE 700
#include <time.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>

#include <fuse.h>
#include <fuse_lowlevel.h>

static FILE *outfile;

static int fs_getattr(const char *path,
                      struct stat *stbuf,
                      struct fuse_file_info *fi) {
	unsigned mode = S_IFREG|0644;
	(void)fi;

	if (0 == memcmp(path, "/", 2)) {
		mode = S_IFDIR|0755;
	}

	stbuf->st_mode = mode;

	return 0;
}

static int fs_open(const char *path, struct fuse_file_info *fi) {
	(void)path;
	(void)fi;

	return 0;
}

static const char *cbasename(const char *path) {
	const char *rv = path;
	const char *p;

	for (p = path; *p; p++) {
		if (*p == '/' && *(p+1)) rv = p+1;
	}

	return rv;
}

static void print_line_with_details(const char *date,
                                    const char *exename,
                                    pid_t pid,
                                    const char *line,
                                    size_t linelen);

#define DELETED_SUF " (deleted)"

static int fs_write(const char *path,
                    const char *buf,
                    size_t size,
                    off_t offset,
                    struct fuse_file_info *fi) {
	char proc_exe[32];
	char exebuf[64];
	char datebuf[32];
	ssize_t read;
	struct fuse_context *ctx = fuse_get_context();
	const char *exename = "?";
	time_t t;
	struct tm *tmp;
	size_t insize = size;
	(void)path;
	(void)offset;
	(void)fi;

	snprintf(proc_exe, sizeof(proc_exe), "/proc/%u/exe", ctx->pid);
	read = readlink(proc_exe, exebuf, sizeof(exebuf));
	if (read >= 0 && (size_t)read < sizeof(exebuf)) {
		exebuf[read] = '\0';
		if ((size_t)read >= strlen(DELETED_SUF) &&
		    0 == memcmp(exebuf+read-strlen(DELETED_SUF), DELETED_SUF, sizeof(DELETED_SUF))) {
			exebuf[(size_t)read-strlen(DELETED_SUF)] = '\0';
		}
		exename = cbasename(exebuf);
	}

	t = time(NULL);
	tmp = localtime(&t);
	strftime(datebuf, sizeof(datebuf), "%Y-%m-%d %H:%M:%S", tmp);

	while (size != 0) {
		const char *eol = memchr(buf, '\n', size);
		if (eol != NULL) {
			size_t linelen = (size_t)(eol-buf);
			print_line_with_details(datebuf, exename, ctx->pid, buf, linelen);
			buf += linelen+1;
			size -= linelen+1;
		} else {
			/* idiot forgot to write the newline */
			size_t linelen = size;
			print_line_with_details(datebuf, exename, ctx->pid, buf, linelen);
			buf += linelen;
			size -= linelen;
		}
	}

	return (int)insize;
}

static void print_line_with_details(const char *date,
                                    const char *exename,
                                    pid_t pid,
                                    const char *line,
                                    size_t linelen) {
	fprintf(outfile, "[%s] %s[%u]: %.*s\n",
	    date, exename, pid, (int)linelen, line);
}

static struct fuse *g_fuse;

static int fs_release(const char *path, struct fuse_file_info *fi) {
	struct fuse *fuse = g_fuse;
	(void)path;
	(void)fi;

	if (fuse != NULL && 0 != memcmp(path, "/", 2)) {
		fuse_exit(fuse);
	}

	return 0;
}

static int commpipe[2];
static const char *mountpoint;

static void *fs_init(struct fuse_conn_info *conn, struct fuse_config *cfg) {
	(void)conn;
	(void)cfg;

	if (-1 == write(commpipe[1], mountpoint, strlen(mountpoint))) {
		perror("fs_init: write");
		close(commpipe[1]);
		commpipe[1] = -1;
	}

	return NULL;
}

static struct fuse_operations fs_ops;

int main(int argc, char **argv) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wc99-extensions"
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
#pragma GCC diagnostic pop
	struct fuse_cmdline_opts opts;
	struct fuse *fuse = NULL;
	struct fuse_session *se = NULL;
	struct fuse_loop_config cfg;
	char tmpmount[64];
	int rv;

	commpipe[0] = -1;
	commpipe[1] = -1;

	fs_ops.getattr = fs_getattr;
	fs_ops.open = fs_open;
	fs_ops.write = fs_write;
	fs_ops.release = fs_release;
	fs_ops.init = fs_init;

	if (NULL != getenv("UNJ_FD")) {
		commpipe[1] = atoi(getenv("UNJ_FD"));
	} else {
		char fdbuf[32];
		pid_t pid;
		int devnull;

		if (-1 == pipe(commpipe)) {
			perror("pipe");
			exit(1);
		}
		snprintf(fdbuf, sizeof(fdbuf), "%d", commpipe[1]);
		setenv("UNJ_FD", fdbuf, 1);

		devnull = open("/dev/null", O_RDWR);
		if (devnull == -1) {
			perror("open /dev/null");
			exit(1);
		}

		pid = fork();
		if (pid == -1) {
			perror("fork");
			exit(1);
		}
		if (pid == 0) {
			dup2(devnull, STDOUT_FILENO);
			close(devnull);
			close(commpipe[0]);
			execvp(argv[0], argv);
			_exit(1);
		} else {
			char pathbuf[128];
			ssize_t sz;

			close(commpipe[1]);
			for (;;) {
				sz = read(commpipe[0], pathbuf, sizeof(pathbuf));
				if (sz == -1) {
					if (errno == EINTR) continue;
					perror("read");
					exit(1);
				}
				break;
			}
			if (sz == 0) {
				exit(1);
			}
			printf("%.*s/output\n", (int)sz, pathbuf);
			exit(0);
		}
	}

	if (0 != fuse_parse_cmdline(&args, &opts)) {
		rv = 1;
		goto out;
	}

	if (opts.mountpoint) {
		free(opts.mountpoint);
		opts.mountpoint = NULL;
	}
	snprintf(tmpmount, sizeof(tmpmount), "/tmp/unjumble.XXXXXX");
	if (NULL == mkdtemp(tmpmount)) {
		perror("mkdtemp");
		exit(1);
	}
	opts.mountpoint = tmpmount;

	if (opts.show_version) {
		fuse_lowlevel_version();
		rv = 0;
		goto out;
	}
	if (opts.show_help) {
		if (args.argv[0][0] != '\0') {
			fprintf(stderr, "usage: %s [options] <mountpoint>\n", args.argv[0]);
		}
		fprintf(stderr, "FUSE options:\n");
		fuse_cmdline_help();
		fuse_lib_help(&args);
		rv = 0;
		goto out;
	}
	if (!opts.mountpoint) {
		fprintf(stderr, "error: no mountpoint specified\n");
		rv = 1;
		goto out;
	}

	mountpoint = opts.mountpoint;
	opts.foreground = 1;

	outfile = fopen("/dev/stdin", "a");
	if (outfile == NULL) {
		perror("fopen");
		rv = 1;
		goto out;
	}
	setvbuf(outfile, NULL, _IONBF, 0);

	fuse = fuse_new(&args, &fs_ops, sizeof(fs_ops), NULL);
	if (fuse == NULL) {
		rv = 1;
		goto out;
	}
	g_fuse = fuse;

	if (0 != fuse_mount(fuse, opts.mountpoint)) {
		rv = 1;
		goto out;
	}

	se = fuse_get_session(fuse);
	if (0 != fuse_set_signal_handlers(se)) {
		rv = 1;
		goto out;
	}

	cfg.clone_fd = 0;
	cfg.max_idle_threads = 5;
	rv = fuse_loop_mt(fuse, &cfg);
out:
	if (se != NULL) {
		fuse_remove_signal_handlers(se);
		se = NULL;
	}

	if (fuse != NULL) {
		g_fuse = NULL;
		fuse_unmount(fuse);
		fuse_destroy(fuse);
		fuse = NULL;
	}

	mountpoint = NULL;
	if (opts.mountpoint != tmpmount) {
		free(opts.mountpoint);
		opts.mountpoint = NULL;
	}
	if (-1 == rmdir(tmpmount)) {
		perror("rmdir");
	}

	fuse_opt_free_args(&args);

	return rv;
}
