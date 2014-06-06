/*
 * daemonlib
 * Copyright (C) 2012 Matthias Bolte <matthias@tinkerforge.com>
 * Copyright (C) 2014 Olaf Lüke <olaf@tinkerforge.com>
 *
 * log.c: Logging specific functions
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

#include <string.h>
#ifndef _MSC_VER
	#include <sys/time.h>
#endif

#include "log.h"

#include "threads.h"

static Mutex _mutex; // protects writing to _file
static int _debug_override = 0;
static LogLevel _levels[MAX_LOG_CATEGORIES]; // log_init initializes this
static FILE *_file = NULL;

extern int _log_debug_override_platform;

extern void log_init_platform(void);
extern void log_exit_platform(void);
extern void log_handler_platform(struct timeval *timestamp,
                                 LogCategory category, LogLevel level,
                                 const char *file, int line,
                                 const char *function, const char *format,
                                 va_list arguments);

// NOTE: assumes that _mutex is locked
static void log_handler(struct timeval *timestamp, LogCategory category,
                        LogLevel level, const char *file, int line,
                        const char *function, const char *format,
                        va_list arguments) {
	time_t t;
	struct tm lt;
	char lt_str[64] = "<unknown>";
	char level_c = 'U';
	const char *category_name = "unknown";

	(void)function;

	// check file
	if (_file == NULL) {
		return;
	}

	// copy value to time_t variable because timeval.tv_sec and time_t
	// can have different sizes between different compilers and compiler
	// version and platforms. for example with WDK 7 both are 4 byte in
	// size, but with MSVC 2010 time_t is 8 byte in size but timeval.tv_sec
	// is still 4 byte in size.
	t = timestamp->tv_sec;

	// format time
	if (localtime_r(&t, &lt) != NULL) {
		strftime(lt_str, sizeof(lt_str), "%Y-%m-%d %H:%M:%S", &lt);
	}

	// format level
	switch (level) {
	case LOG_LEVEL_NONE:  level_c = 'N'; break;
	case LOG_LEVEL_ERROR: level_c = 'E'; break;
	case LOG_LEVEL_WARN:  level_c = 'W'; break;
	case LOG_LEVEL_INFO:  level_c = 'I'; break;
	case LOG_LEVEL_DEBUG: level_c = 'D'; break;
	}

	// format category
	switch (category) {
	case LOG_CATEGORY_EVENT:     category_name = "event";     break;
	case LOG_CATEGORY_USB:       category_name = "usb";       break;
	case LOG_CATEGORY_NETWORK:   category_name = "network";   break;
	case LOG_CATEGORY_HOTPLUG:   category_name = "hotplug";   break;
	case LOG_CATEGORY_HARDWARE:  category_name = "hardware";  break;
	case LOG_CATEGORY_WEBSOCKET: category_name = "websocket"; break;
	case LOG_CATEGORY_OTHER:     category_name = "other";     break;
	case LOG_CATEGORY_API:       category_name = "api";       break;
	case LOG_CATEGORY_LIBUSB:    category_name = "libusb";    break;
	}

	// print prefix
	fprintf(_file, "%s.%06d <%c> <%s|%s:%d> ",
	        lt_str, (int)timestamp->tv_usec, level_c, category_name, file, line);

	// print message
	vfprintf(_file, format, arguments);
	fprintf(_file, "\n");
	fflush(_file);
}

void log_init(void) {
	int i;

	mutex_create(&_mutex);

	for (i = 0; i < MAX_LOG_CATEGORIES; ++i) {
		_levels[i] = LOG_LEVEL_INFO;
	}

	_file = stderr;

	log_init_platform();
}

void log_exit(void) {
	log_exit_platform();

	mutex_destroy(&_mutex);
}

void log_set_debug_override(int override) {
	_debug_override = override;
}

void log_set_level(LogCategory category, LogLevel level) {
	if (category != LOG_CATEGORY_LIBUSB) {
		_levels[category] = level;
	}
}

LogLevel log_get_effective_level(LogCategory category) {
	if (_debug_override || _log_debug_override_platform || category == LOG_CATEGORY_LIBUSB) {
		return LOG_LEVEL_DEBUG;
	} else {
		return _levels[category];
	}
}

void log_set_file(FILE *file) {
	mutex_lock(&_mutex);

	_file = file;

	mutex_unlock(&_mutex);
}

FILE *log_get_file(void) {
	return _file;
}

void log_message(LogCategory category, LogLevel level, const char *file, int line,
                 const char *function, const char *format, ...) {
	struct timeval timestamp;
	const char *p;
	va_list arguments;

	// record timestamp before locking the mutex. this results in more accurate
	// timing of log message if the mutex is contended
	if (gettimeofday(&timestamp, NULL) < 0) {
		timestamp.tv_sec = time(NULL);
		timestamp.tv_usec = 0;
	}

	// only keep last part of filename
	p = strrchr(file, '/');

	if (p != NULL) {
		file = p + 1;
	}

	p = strrchr(file, '\\');

	if (p != NULL) {
		file = p + 1;
	}

	// call log handlers
	va_start(arguments, format);
	mutex_lock(&_mutex);

	if (_debug_override || level <= _levels[category]) {
		log_handler(&timestamp, category, level, file, line, function, format, arguments);
	}

	if (_debug_override || _log_debug_override_platform || level <= _levels[category]) {
		log_handler_platform(&timestamp, category, level, file, line, function, format, arguments);
	}

	mutex_unlock(&_mutex);
	va_end(arguments);
}
