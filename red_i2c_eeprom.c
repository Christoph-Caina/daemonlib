/*
 * daemonlib
 * Copyright (C) 2014 Ishraq Ibne Ashraf <ishraq@tinkerforge.com>
 * Copyright (C) 2014 Matthias Bolte <matthias@tinkerforge.com>
 * Copyright (C) 2014 Olaf Lüke <olaf@tinkerforge.com>
 *
 * red_i2c_eeprom.c: I2C EEPROM specific functions
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

#include "red_i2c_eeprom.h"

#include <errno.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <sys/ioctl.h>

#include "red_gpio.h"
#include "log.h"
#include "utils.h"

#define LOG_CATEGORY LOG_CATEGORY_RED_BRICK

void _i2c_eeprom_select(I2CEEPROM *i2c_eeprom) {
    // address pin high
    gpio_output_set(i2c_eeprom->address_pin);
}

void _i2c_eeprom_deselect(I2CEEPROM *i2c_eeprom) {
    // address pin low
    gpio_output_clear(i2c_eeprom->address_pin);
}

int _i2c_eeprom_set_pointer(I2CEEPROM *i2c_eeprom, uint8_t* eeprom_memory_address) {
    int bytes_written = 0;

	if(i2c_eeprom == NULL || i2c_eeprom->file < 0) {
		log_error("I2C EEPROM structure uninitialized\n");
		return -1;
	}

    bytes_written = write(i2c_eeprom->file, eeprom_memory_address, 2);
    if ( bytes_written != 2) {
    	// We only use debug here to not spam the log with errors.
    	// This is the expected case if an extension is not present.
        log_debug("Error setting EEPROM address pointer: %s (%d)",
                  get_errno_name(errno), errno);

        i2c_eeprom_release(i2c_eeprom);
        return -1;
    }
    return bytes_written;
}

// TODO: If we want "real parallel accessibility" of the EEPROM we need to
//       lock a mutex in the init function and unlock it in the release function
int i2c_eeprom_init(I2CEEPROM *i2c_eeprom, int extension) {
	log_debug("Initializing I2C EEPROM for extension %d", extension);
	if(i2c_eeprom == NULL || extension < 0 || extension > 1) {
		log_error("Initialization of I2C EEPROM for extension %d failed (malformed parameters)",
		          extension);
		return -1;
	}

    // Enable pullups
	GPIOPin pullup = {GPIO_PORT_B, GPIO_PIN_6};
    gpio_mux_configure(pullup, GPIO_MUX_OUTPUT);
    gpio_output_clear(pullup);

	// Initialize I2C EEPROM structure
	i2c_eeprom->extension = extension;
	switch(extension) {
		case 0:
			i2c_eeprom->address_pin.port_index = GPIO_PORT_G;
			i2c_eeprom->address_pin.pin_index = GPIO_PIN_9;
			break;
		case 1:
			i2c_eeprom->address_pin.port_index = GPIO_PORT_G;
			i2c_eeprom->address_pin.pin_index = GPIO_PIN_13;
			break;
	}

    // enable I2C bus with GPIO
    gpio_mux_configure(i2c_eeprom->address_pin, GPIO_MUX_OUTPUT);
    _i2c_eeprom_deselect(i2c_eeprom);
    
    i2c_eeprom->file = open(I2C_EEPROM_BUS, O_RDWR);

    if (i2c_eeprom->file < 0) {
        log_error("Initialization of I2C EEPROM for extension %d failed (Unable to open I2C bus: %s (%d))",
                  extension, get_errno_name(errno), errno);

        return -1;
    }
    
    if (ioctl(i2c_eeprom->file, I2C_SLAVE, I2C_EEPROM_DEVICE_ADDRESS) < 0) {
        log_error("Initialization of I2C EEPROM for extension %d failed (Unable to access I2C device on the bus: %s (%d))",
                  extension, get_errno_name(errno), errno);

        i2c_eeprom_release(i2c_eeprom);
        return -1;
    }

    return 0;
}

void i2c_eeprom_release(I2CEEPROM *i2c_eeprom) {
	log_debug("Releasing I2C EEPROM for extension %d", i2c_eeprom->extension);

	if(i2c_eeprom != NULL) {
        _i2c_eeprom_deselect(i2c_eeprom);
		close(i2c_eeprom->file);
        i2c_eeprom->file = -1;
	}
}

int i2c_eeprom_read(I2CEEPROM *i2c_eeprom, uint16_t eeprom_memory_address,
                    uint8_t* buffer_to_store, int bytes_to_read) {
    int bytes_read = 0;
	if(i2c_eeprom == NULL || i2c_eeprom->file < 0) {
		log_error("I2C EEPROM structure uninitialized\n");
		return -1;
	}

    uint8_t mem_address[2] = {eeprom_memory_address >> 8,
                              eeprom_memory_address & 0x00FF};

    
    _i2c_eeprom_select(i2c_eeprom);
    if((_i2c_eeprom_set_pointer(i2c_eeprom, mem_address)) < 0) {
        return -1;
    }

    bytes_read = read(i2c_eeprom->file, buffer_to_store, bytes_to_read);
    if(bytes_read != bytes_to_read) {
        log_error("EEPROM read failed: %s (%d)", get_errno_name(errno), errno);

        i2c_eeprom_release(i2c_eeprom);
        return -1;
    }

    _i2c_eeprom_deselect(i2c_eeprom);
    return bytes_read;
}

int i2c_eeprom_write(I2CEEPROM *i2c_eeprom, uint16_t eeprom_memory_address,
                     uint8_t* buffer_to_write, int bytes_to_write) {
    int i = 0;
    char _bytes_written = 0;
    char bytes_written = 0;
    uint8_t write_byte[3] = {0};

	if(i2c_eeprom == NULL || i2c_eeprom->file < 0) {
		log_error("I2C EEPROM structure uninitialized\n");
		return -1;
	}
    
    for(i = 0; i < bytes_to_write; i++) {
        write_byte[0] = eeprom_memory_address >> 8;
        write_byte[1] = eeprom_memory_address & 0xFF;
        write_byte[2] = buffer_to_write[i];

    	_i2c_eeprom_select(i2c_eeprom);
        _bytes_written = write(i2c_eeprom->file, write_byte, 3);
        _i2c_eeprom_deselect(i2c_eeprom);

        // Wait at least 5ms between writes (see m24128-bw.pdf)
        usleep(5*1000);
        printf("pos: %d\n", i);

        if(_bytes_written != 3) {
            log_error("EEPROM write failed (pos(%d), length(%d), expected length(%d): %s (%d)",
            		  i, _bytes_written, 3, get_errno_name(errno), errno);

            i2c_eeprom_release(i2c_eeprom);
            return -1;
        }
        eeprom_memory_address++;
        bytes_written++;
    }


    return bytes_written;
}
