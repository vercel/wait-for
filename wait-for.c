#define _POSIX_SOURCE
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <libgen.h>
#include <limits.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "ext/xopt/xopt.h"

typedef struct cli_args {
	bool help;
	bool execute;
	bool read;
	bool write;
	bool pipe;
	bool socket;
	bool regular;
	bool directory;
	char *username;
} cli_args;

static const xoptOption options[] = {
	{
		"help",
		'h',
		offsetof(cli_args, help),
		0,
		XOPT_TYPE_BOOL,
		0,
		"Shows this help message"
	},
	{
		"execute",
		'x',
		offsetof(cli_args, execute),
		0,
		XOPT_TYPE_BOOL,
		0,
		"Wait for the path to become executable"
	},
	{
		"read",
		'r',
		offsetof(cli_args, read),
		0,
		XOPT_TYPE_BOOL,
		0,
		"Wait for the path to become readable"
	},
	{
		"write",
		'w',
		offsetof(cli_args, write),
		0,
		XOPT_TYPE_BOOL,
		0,
		"Wait for the path to become writable"
	},
	{
		"pipe",
		'p',
		offsetof(cli_args, pipe),
		0,
		XOPT_TYPE_BOOL,
		0,
		"Wait for the path to be a pipe (FIFO)"
	},
	{
		"socket",
		's',
		offsetof(cli_args, socket),
		0,
		XOPT_TYPE_BOOL,
		0,
		"Wait for the path to be a socket"
	},
	{
		"file",
		'f',
		offsetof(cli_args, regular),
		0,
		XOPT_TYPE_BOOL,
		0,
		"Wait for the path to be a regular file"
	},
	{
		"directory",
		'd',
		offsetof(cli_args, directory),
		0,
		XOPT_TYPE_BOOL,
		0,
		"Wait for the path to be a directory"
	},
	{
		"username",
		'U',
		offsetof(cli_args, username),
		0,
		XOPT_TYPE_STRING,
		0,
		"The username to run access checks for (NOT the user ID)"
	},
	XOPT_NULLOPTION
};

static int is_satisfactory(const cli_args *restrict args, const char *username, uid_t uid, gid_t gid, const char *restrict file) {
	struct stat stats;

	if (stat(file, &stats) == -1) {
		if (errno == ENOENT || errno == EACCES || errno == ENOTDIR || errno == ETXTBSY) {
			// not an 'error', but simply not satisfactory; keep waiting
			fprintf(stderr, "wait-for %s: is_satisfactory errno=%d\n", file, errno);
			return 0;
		}

		perror("could not stat awaited file");
		return -1;
	}

	bool has_type_preference = args->socket || args->directory || args->regular || args->pipe;
	fprintf(stderr, "wait-for %s: has_type_preference=%d\n", file, has_type_preference);
	if (has_type_preference) {
		fprintf(stderr, "wait-for %s: is_satisfactory mode=%d\n", file, stats.st_mode & S_IFMT);
		switch (stats.st_mode & S_IFMT) {
		case S_IFIFO: return args->pipe ? 1 : 0;
		case S_IFDIR: return args->directory ? 1 : 0;
		case S_IFREG: return args->regular ? 1 : 0;
		case S_IFSOCK: return args->socket ? 1 : 0;
		default: return 0;
		}
	}

	bool is_in_group = false;
	bool is_owner = stats.st_uid == uid;

	gid_t groups[32]; // strong evidence to say that a user can only be part of 32 at a time.
	int ngroups = sizeof(groups) / sizeof(groups[0]);

	if (getgrouplist(username, gid, &groups[0], &ngroups) == -1) {
		perror("could not retrieve list of user groups");
		return -1;
	}

	for (int i = 0; i < ngroups; i++) {
		if (groups[i] == stats.st_gid) {
			is_in_group = true;
			break;
		}
	}

	bool satisfied = true;

	if (satisfied && args->read) {
		satisfied = (is_owner && (stats.st_mode & S_IRUSR))
			|| (is_in_group && (stats.st_mode & S_IRGRP))
			|| (stats.st_mode & S_IROTH);
	}

	if (satisfied && args->write) {
		satisfied = (is_owner && (stats.st_mode & S_IWUSR))
			|| (is_in_group && (stats.st_mode & S_IWGRP))
			|| (stats.st_mode & S_IWOTH);
	}

	if (satisfied && args->execute) {
		satisfied = (is_owner && (stats.st_mode & S_IXUSR))
			|| (is_in_group && (stats.st_mode & S_IXGRP))
			|| (stats.st_mode & S_IXOTH);
	}

	fprintf(stderr, "wait-for %s: is_satisfactory satisfied=%d\n", file, satisfied);
	return satisfied;
}

#define BUF_LEN (10 * (sizeof(struct inotify_event) + NAME_MAX + 1))

int main(int argc, const char **argv) {
	int status = 0;
	int inotify_fd = -1;
	int watch_fd = -1;
	char dirpath[PATH_MAX];
	char username[LOGIN_NAME_MAX];
	uid_t uid = 0;
	gid_t gid = 0;

	cli_args args;
	memset(&args, 0, sizeof(args));

	const char *err = NULL;
	int extrac = 0;
	const char **extrav = NULL;
	XOPT_SIMPLE_PARSE(
		argv[0],
		&options[0], &args,
		argc, argv,
		&extrac, &extrav,
		&err,
		stderr,
		"wait-for [--help] [-rwx] <file>",
		"Wait for a file to exist and optionally have one or modes",
		"If multiple modes are specified, wait-for waits for all of them to become available.\n"
		"If multiple file types are specified, wait-for waits for the file to be any one of the specified types.",
		5);

	if (err != NULL) {
		fprintf(stderr, "error: %s\n", err);
		status = 1;
		goto exit;
	}

	if (extrac != 1) {
		fprintf(stderr, "error: expected exactly one positional argument (the file to wait for) - got %d\n", extrac);
		status = 2;
		goto exit;
	}

	if (args.username == NULL) {
		// attempt to populate based on current UID
		struct passwd *pwd = getpwuid(getuid());
		if (pwd == NULL) {
			perror("could not get passwd entry for the user");
			status = 1;
			goto exit;
		}

		strncpy(username, pwd->pw_name, sizeof(username));
		uid = pwd->pw_uid;
		gid = pwd->pw_gid;
	} else if (strnlen(args.username, 1) == 0) {
		fputs("error: username cannot be zero-length\n", stderr);
		status = 2;
		goto exit;
	}

	inotify_fd = inotify_init();
	if (inotify_fd == -1) {
		perror("could not initialize inotify subsystem");
		status = 1;
		goto exit;
	}

	// we try to initialize the watch for the directory
	// if that fails, we fall back to a standard polling mechanism
	// which isn't as efficient
	strcpy(dirpath, extrav[0]);
	if (dirname(dirpath) == NULL) {
		fprintf(stderr, "warning: could not get dirname of the given path (falling back to poll mechanism): %s\n", strerror(errno));
		goto fallback;
	}

	watch_fd = inotify_add_watch(inotify_fd, dirpath, IN_CREATE | IN_ATTRIB | IN_MODIFY | IN_MOVED_FROM | IN_MOVED_TO);
	fprintf(stderr, "wait-for %s: inotify_add_watch=%d\n", extrav[0], watch_fd);
	if (watch_fd == -1) {
		if (errno != ENOENT) {
			// nothing we can really do
			fprintf(stderr, "warning: could not initialize watch handle (falling back to poll mechanism): %s\n", strerror(errno));
		}
		goto fallback;
	}

	char in_buf[BUF_LEN] __attribute__ ((aligned(8)));

	for (;;) {
		// since we don't actually care about the event contents
		// we simply want to check the stat of the file.
		// we also do this first so that the initial check
		// is performed regardless of inotify stuff.
		int i = is_satisfactory(&args, username, uid, gid, extrav[0]);
		fprintf(stderr, "wait-for %s: is_satisfactory=%d\n", extrav[0], i);
		switch (i) {
		case 1:
			status = 0;
			goto exit;
		case 0:
			break;
		case -1:
			// error already emitted
			status = 1;
			goto exit;
		}

		int numread = read(inotify_fd, in_buf, BUF_LEN);
		fprintf(stderr, "wait-for %s: read=%d\n", extrav[0], numread);
		if (numread == 0) {
			fputs("error: inotify hung up (EOF) (\?\?\? this shouldn't happen)\n", stderr);
			status = 1;
			goto exit;
		}

		if (numread == -1 ) {
			perror("inotify read failed");
			status = 1;
			goto exit;
		}
	}

	assert(false && "aborting before hitting the second loop - the code is missing a goto");

fallback:
	fprintf(stderr, "wait-for %s: fallback\n", extrav[0]);
	// at this point, inotify couldn't be initialized.
	// shut it down, shut it all down -- then do a manual loop.
	if (watch_fd != -1) {
		close(watch_fd);
		watch_fd = -1;
	}

	if (inotify_fd != -1) {
		close(inotify_fd);
		inotify_fd = -1;
	}

	for (;;) {
		int i = is_satisfactory(&args, username, uid, gid, extrav[0]);
		fprintf(stderr, "wait-for %s: fallback is_satisfactory=%d\n", extrav[0], i);
		switch (i) {
		case 1:
			status = 0;
			goto exit;
		case 0:
			break;
		case -1:
			// error already emitted
			status = 1;
			goto exit;
		}

		// only specified error is EINTR, which we don't care about.
		usleep(10000);
	}

exit:
	if (extrav != NULL) {
		free(extrav);
	}

	if (watch_fd != -1) {
		close(watch_fd);
	}

	if (inotify_fd != -1) {
		close(inotify_fd);
	}

	fprintf(stderr, "wait-for %s: exit status=%d\n", extrav[0], status);
	return status;
xopt_help:
	status = 2;
	goto exit;
}
