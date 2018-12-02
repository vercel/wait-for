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

static int is_satisfactory(const cli_args *restrict args, const struct passwd *pwd, const char *restrict file) {
	struct stat stats;

	if (stat(file, &stats) == -1) {
		if (errno == ENOENT || errno == EACCES || errno == ENOTDIR || errno == ETXTBSY) {
			// not an 'error', but simply not satisfactory; keep waiting
			return 0;
		}

		perror("could not stat awaited file");
		return -1;
	}

	bool has_type_preference = args->socket || args->directory || args->regular || args->pipe;
	if (has_type_preference) {
		bool type_satisfied;
		switch (stats.st_mode & S_IFMT) {
			case S_IFIFO: type_satisfied = args->pipe; break;
			case S_IFDIR: type_satisfied = args->directory; break;
			case S_IFREG: type_satisfied = args->regular; break;
			case S_IFSOCK: type_satisfied = args->socket; break;
			default: type_satisfied = false;
		}

		if (!type_satisfied) {
			return 0;
		}
	}

	bool is_owner = stats.st_uid == pwd->pw_uid;

	int ngroups = 0;
	if (getgrouplist(pwd->pw_name, pwd->pw_gid, NULL, &ngroups) != -1 || ngroups <= 0) {
		perror("could not count number of user groups");
		return -1;
	}

	gid_t *groups = malloc(ngroups * sizeof(gid_t));
	if (!groups) {
		perror("no memory left for list of user groups");
		return -1;
	}

	if (getgrouplist(pwd->pw_name, pwd->pw_gid, groups, &ngroups) == -1) {
		perror("could not retrieve list of user groups");
		return -1;
	}

	bool is_in_group = false;
	for (int i = 0; i < ngroups; i++) {
		if (groups[i] == stats.st_gid) {
			is_in_group = true;
			break;
		}
	}

	free(groups);
	groups = NULL;

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

	return satisfied;
}

#define BUF_LEN (10 * (sizeof(struct inotify_event) + NAME_MAX + 1))

int main(int argc, const char **argv) {
	int status = 0;
	int inotify_fd = -1;
	int watch_fd = -1;
	char *dirpath_buffer = NULL;
	struct passwd *pwd = NULL;

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
		"wait-for [--help] [-rwx] [-dfps] [-U <username>] <file>",
		"Wait for a file to exist and optionally have one or modes",
		"If multiple modes are specified, wait-for waits for all of them to become available.\n"
		"If multiple file types are specified, wait-for waits for the file to be any one of the specified types.",
		5);

	if (err != NULL) {
		fprintf(stderr, "error: %s\n", err);
		status = 1;
		goto exit;
	}

	if (extrac < 1) {
		fputs("error: missing file argument\n", stderr);
		status = 2;
		goto exit;
	}

	if (extrac > 1) {
		fputs("error: too many arguments\n", stderr);
		status = 2;
		goto exit;
	}

	if (args.username == NULL) {
		pwd = getpwuid(getuid());
		if (pwd == NULL) {
			perror("could not get passwd entry for the user");
			status = 1;
			goto exit;
		}
	} else {
		errno = 0;
		pwd = getpwnam(args.username);
		if (pwd == NULL) {
			if (errno == 0) {
				fprintf(stderr, "error: user '%s' not found\n", args.username);
				status = 2;
				goto exit;
			} else {
				fprintf(stderr, "error: could not get passwd entry for user '%s': %s\n", args.username, strerror(errno));
				status = 1;
				goto exit;
			}
		}
	}

	inotify_fd = inotify_init();
	if (inotify_fd == -1) {
		perror("could not initialize inotify subsystem");
		status = 1;
		goto exit;
	}

	dirpath_buffer = strdup(extrav[0]);
	if (dirpath_buffer == NULL) {
		fputs("error: out of memory\n", stderr);
		status = 1;
		goto exit;
	}

	const char *dirpath = dirname(dirpath_buffer);

	assert(dirpath != NULL);

	// we try to initialize the watch for the directory
	// if that fails, we fall back to a standard polling mechanism
	// which isn't as efficient
	watch_fd = inotify_add_watch(inotify_fd, dirpath, IN_CREATE | IN_ATTRIB | IN_MODIFY | IN_MOVED_FROM | IN_MOVED_TO);
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
		switch (is_satisfactory(&args, pwd, extrav[0])) {
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
		switch (is_satisfactory(&args, pwd, extrav[0])) {
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
	if (dirpath_buffer != NULL) {
		free(dirpath_buffer);
	}

	if (extrav != NULL) {
		free(extrav);
	}

	if (watch_fd != -1) {
		close(watch_fd);
	}

	if (inotify_fd != -1) {
		close(inotify_fd);
	}

	return status;
xopt_help:
	status = 2;
	goto exit;
}
