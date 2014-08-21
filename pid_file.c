/*
 * daemonlib
 * Copyright (C) 2012-2014 Matthias Bolte <matthias@tinkerforge.com>
 *
 * pid_file.c: PID file specific functions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/*
 * this functions realize a flock'ed PID file.
 */

#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include "pid_file.h"

#include "utils.h"

// returns -1 on error, -2 is pid file is already acquired or pid file fd on success
int pid_file_acquire(const char *filename, pid_t pid) {
	int fd = -1;
	struct stat stat1;
	struct stat stat2;
	struct flock flock;
	char buffer[64];

	for (;;) {
		// open pid file
		fd = open(filename, O_WRONLY | O_CREAT, 0644);

		if (fd < 0) {
			fprintf(stderr, "Could not open PID file '%s': %s (%d)\n",
			        filename, get_errno_name(errno), errno);

			return -1;
		}

		// get pid file status
		if (fstat(fd, &stat1) < 0) {
			fprintf(stderr, "Could not get status of PID file '%s': %s (%d)\n",
			        filename, get_errno_name(errno), errno);

			close(fd);

			return -1;
		}

		// lock pid file
		flock.l_type = F_WRLCK;
		flock.l_whence = SEEK_SET;
		flock.l_start = 0;
		flock.l_len = 1;

		if (fcntl(fd, F_SETLK, &flock) < 0) {
			if (!errno_would_block()) {
				fprintf(stderr, "Could not lock PID file '%s': %s (%d)\n",
				        filename, get_errno_name(errno), errno);
			}

			close(fd);

			return errno_would_block() ? PID_FILE_ALREADY_ACQUIRED : -1;
		}

		// get pid file status again
		if (stat(filename, &stat2) < 0) {
			close(fd);

			continue;
		}

		// if the inode mismatches then the file that got locked is not the
		// one that was opened before, try again
		if (stat1.st_ino != stat2.st_ino) {
			close(fd);

			continue;
		}

		break;
	}

	snprintf(buffer, sizeof(buffer), "%"PRIi64, (int64_t)pid);

	if (write(fd, buffer, strlen(buffer)) < 0) {
		fprintf(stderr, "Could not write to PID file '%s': %s (%d)\n",
		        filename, get_errno_name(errno), errno);

		close(fd);

		return -1;
	}

	return fd;
}

void pid_file_release(const char *filename, int fd) {
	unlink(filename);
	close(fd);
}
