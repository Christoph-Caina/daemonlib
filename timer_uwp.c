/*
 * daemonlib
 * Copyright (C) 2016 Matthias Bolte <matthias@tinkerforge.com>
 *
 * timer_uwp.c: Universal Windows Platform timer implementation
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

#include <errno.h>

#include "timer_uwp.h"

#include "event.h"
#include "log.h"
#include "utils.h"

static LogSource _log_source = LOG_SOURCE_INITIALIZER;

static void timer_handle_read(void *opaque) {
	Timer *timer = opaque;
	uint32_t configuration_id;

	if (pipe_read(&timer->notification_pipe, &configuration_id,
	              sizeof(configuration_id)) < 0) {
		log_error("Could not read from notification pipe of interrupt event (handle: %p): %s (%d)",
		          timer->interrupt_event, get_errno_name(errno), errno);

		return;
	}

	if (configuration_id != timer->configuration_id) {
		log_debug("Ignoring timer event for mismatching configuration of interrupt event (handle: %p)",
		          timer->interrupt_event);

		return;
	}

	timer->function(timer->opaque);
}

static void timer_thread(void *opaque) {
	Timer *timer = opaque;
	int rc;
	uint32_t configuration_id = timer->configuration_id;
	uint64_t delay = 0; // in milliseconds
	uint64_t interval = 0; // in milliseconds
	DWORD timeout = INFINITE; // in milliseconds

	while (timer->running) {
		rc = WaitForSingleObject(timer->interrupt_event, timeout);

		if (rc == WAIT_TIMEOUT) { // timer
			if (delay == 0 && interval == 0) {
				log_debug("Ignoring timer event for inactive interrupt event (handle: %p)",
				          timer->interrupt_event);

				continue;
			}

			if (pipe_write(&timer->notification_pipe, &configuration_id,
			               sizeof(configuration_id)) < 0) {
				log_error("Could not write to notification pipe of interrupt event (handle: %p): %s (%d)",
				          timer->interrupt_event, get_errno_name(errno), errno);

				break;
			}

			timeout = interval;
		} else if (rc == WAIT_OBJECT_0) { // interrupt
			if (!timer->running) {
				break;
			}

			if (timer->delay == 0 && timer->interval == 0) {
				delay = 0;
				interval = 0;
				timeout = INFINITE;
			} else {
				// convert from microseconds to milliseconds
				if (timer->delay == 0) {
					delay = 0;
				} else if (timer->delay < 1000) {
					delay = 1;
				} else {
					delay = (timer->delay + 500) / 1000;
				}

				if (timer->interval == 0) {
					interval = 0;
				} else if (timer->interval < 1000) {
					interval = 1;
				} else {
					interval = (timer->interval + 500) / 1000;
				}

				timeout = delay;
			}

			configuration_id = timer->configuration_id;

			semaphore_release(&timer->handshake);
		} else {
			rc = ERRNO_WINAPI_OFFSET + GetLastError();

			log_error("Could not wait for interrupt event (handle: %p): %s (%d)",
			          timer->interrupt_event, get_errno_name(rc), rc);

			break;
		}
	}

	timer->running = false;

	semaphore_release(&timer->handshake);
}

int timer_create_(Timer *timer, TimerFunction function, void *opaque) {
	int phase = 0;
	int rc;

	// create notification pipe
	if (pipe_create(&timer->notification_pipe, 0) < 0) {
		log_error("Could not create notification pipe: %s (%d)",
		          get_errno_name(errno), errno);

		goto cleanup;
	}

	phase = 1;

	// create interrupt event
	timer->interrupt_event = CreateEvent(NULL, FALSE, FALSE, NULL);

	if (timer->interrupt_event == NULL) {
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		log_error("Could not create event: %s (%d)",
		          get_errno_name(rc), rc);

		goto cleanup;
	}

	phase = 2;

	// register notification pipe as event source
	timer->function = function;
	timer->opaque = opaque;

	if (event_add_source(timer->notification_pipe.read_end, EVENT_SOURCE_TYPE_GENERIC,
	                     EVENT_READ, timer_handle_read, timer) < 0) {
		goto cleanup;
	}

	phase = 3;

	// create thread
	timer->running = true;
	timer->delay = 0;
	timer->interval = 0;
	timer->configuration_id = 0;

	semaphore_create(&timer->handshake);
	thread_create(&timer->thread, timer_thread, timer);

	log_debug("Created interrupt event (handle: %p)", timer->interrupt_event);

cleanup:
	switch (phase) { // no breaks, all cases fall through intentionally
	case 2:
		CloseHandle(timer->interrupt_event);

	case 1:
		pipe_destroy(&timer->notification_pipe);

	default:
		break;
	}

	return phase == 3 ? 0 : -1;
}

void timer_destroy(Timer *timer) {
	int rc;

	log_debug("Destroying interrupt event (handle: %p)", timer->interrupt_event);

	if (timer->running) {
		timer->running = false;

		if (!SetEvent(timer->interrupt_event)) {
			rc = ERRNO_WINAPI_OFFSET + GetLastError();

			log_error("Could not interrupt thread for interrupt event (handle: %p): %s (%d)",
			          timer->interrupt_event, get_errno_name(rc), rc);
		} else {
			thread_join(&timer->thread);
		}
	}

	event_remove_source(timer->notification_pipe.read_end, EVENT_SOURCE_TYPE_GENERIC);

	semaphore_destroy(&timer->handshake);

	CloseHandle(timer->interrupt_event);

	pipe_destroy(&timer->notification_pipe);
}

// setting delay and interval to 0 stops the timer
int timer_configure(Timer *timer, uint64_t delay, uint64_t interval) { // microseconds
	int rc;

	if (!timer->running) {
		log_error("Thread for interrupt event (handle: %p) is not running",
		          timer->interrupt_event);

		return -1;
	}

	timer->delay = delay;
	timer->interval = interval;

	++timer->configuration_id;

	if (!SetEvent(timer->interrupt_event)) {
		rc = ERRNO_WINAPI_OFFSET + GetLastError();

		log_error("Could not interrupt thread for interrupt event (handle: %p): %s (%d)",
		          timer->interrupt_event, get_errno_name(rc), rc);

		return -1;
	}

	semaphore_acquire(&timer->handshake);

	if (!timer->running) {
		log_error("Thread for interrupt event (handle: %p) exited due to an error",
		          timer->interrupt_event);

		return -1;
	}

	return 0;
}