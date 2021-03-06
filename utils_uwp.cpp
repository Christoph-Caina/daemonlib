/*
 * daemonlib
 * Copyright (C) 2017 Matthias Bolte <matthias@tinkerforge.com>
 *
 * utils_uwp.c: Utility functions for Universal Windows Platform
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

extern "C" {

#include "utils_uwp.h"

}

// sets errno on error
extern "C" char *string_convert_ascii(Platform::String ^string) {
	int length = string->Length();
	const wchar_t *data = string->Data();
	int i;
	char *ascii = (char *)calloc(length + 1, 1);

	if (ascii == nullptr) {
		return nullptr;
	}

	for (i = 0; i < length; ++i) {
		if (data[i] < 32 || data[i] > 126) {
			ascii[i] = '?';
		} else {
			ascii[i] = (char)data[i];
		}
	}

	return ascii;
}