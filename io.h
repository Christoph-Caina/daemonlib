/*
 * daemonlib
 * Copyright (C) 2014 Matthias Bolte <matthias@tinkerforge.com>
 *
 * io.h: Base for all I/O devices
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

#ifndef DAEMONLIB_IO_H
#define DAEMONLIB_IO_H

#ifdef _WIN32
	#include <winsock2.h>
#endif

#ifdef _WIN32
typedef SOCKET IOHandle;
#else
typedef int IOHandle;
#endif

#ifdef _WIN32
	#define IO_HANDLE_INVALID INVALID_SOCKET
#else
	#define IO_HANDLE_INVALID (-1)
#endif

#define IO_CONTINUE (-2)

typedef struct IO_ IO;

typedef int (*IOReadFunction)(IO *io, void *buffer, int length);
typedef int (*IOWriteFunction)(IO *io, void *buffer, int length);
typedef int (*IODestroyFunction)(IO *io);

struct IO_ {
	IOHandle handle;
	const char *type; // for display purpose
	IODestroyFunction destroy;
	IOReadFunction read;
	IOWriteFunction write;
};

int io_create(IO *io, const char *type,
              IODestroyFunction destroy,
              IOReadFunction read,
              IOWriteFunction write);
void io_destroy(IO *io);

int io_read(IO *io, void *buffer, int length);
int io_write(IO *io, void *buffer, int length);

#endif // DAEMONLIB_IO_H
