#define _POSIX_SOURCE
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <unistd.h>

#include "ext/xopt/xopt.h"

typedef struct cli_args {
	bool help;
	bool execute;
	bool read;
	bool write;
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
		"Wait for the file to become executable"
	},
	{
		"read",
		'r',
		offsetof(cli_args, read),
		0,
		XOPT_TYPE_BOOL,
		0,
		"Wait for the file to become readable"
	},
	{
		"write",
		'w',
		offsetof(cli_args, write),
		0,
		XOPT_TYPE_BOOL,
		0,
		"Wait for the file to become writable"
	},
	XOPT_NULLOPTION
};

static int is_satisfactory(const cli_args *restrict args, const char *restrict file) {
	assert(F_OK == 0);

	int mode = (args->execute ? X_OK : 0)
		| (args->read ? R_OK : 0)
		| (args->write ? W_OK : 0);

	if (access(file, mode) == -1) {
		if (errno == ENOENT || errno == EACCES || errno == ENOTDIR || errno == ETXTBSY) {
			// not an 'error', but simply not satisfactory; keep waiting
			return 0;
		}

		perror("could not check access of awaited file");
		return -1;
	}

	return 1;
}

#define BUF_LEN (10 * (sizeof(struct inotify_event) + NAME_MAX + 1))

int main(int argc, const char **argv) {
	int status = 0;
	int inotify_fd = -1;
	int watch_fd = -1;
	char dirpath[PATH_MAX];

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
		"Waits for a file to exist and optionally have one or modes",
		"If multiple modes are specified, wait-for waits for all of them to become available",
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
		switch (is_satisfactory(&args, extrav[0])) {
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
		switch (is_satisfactory(&args, extrav[0])) {
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

	return status;
xopt_help:
	status = 2;
	goto exit;
}
