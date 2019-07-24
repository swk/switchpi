/* 
 * FreeSWITCH Modular Media Switching Software Library / Soft-Switch Application
 * Copyright (C) 2005-2019, Anthony Minessale II <anthmct@yahoo.com>
 *
 * Version: MPL 1.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is FreeSWITCH Modular Media Switching Software Library / Soft-Switch Application
 *
 * The Initial Developer of the Original Code is
 * Ken Rice <krice@freeswitch.org>
 * Portions created by the Initial Developer are Copyright (C)
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 * Ken Rice <krice@freeswitch.org> (C)2013-2018 
 *
 * mod_switchpi.c -- DTMF Keypad, and hook switch driver for mod_portaudio
 * NOTE: this only works on the Raspberry Pi with the SwitchPI addon module
 *
 */
#include <switch.h>

/* not sure which of these we actually need 
   we'll comment them out later*/
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>

#include <wiringPi.h>
#include <wiringPiI2C.h>

// MCP23S17 Registers

#define IOCON           0x0A

#define IODIRA          0x00
#define IPOLA           0x02
#define GPINTENA        0x04
#define DEFVALA         0x06
#define INTCONA         0x08
#define GPPUA           0x0C
#define INTFA           0x0E
#define INTCAPA         0x10
#define GPIOA           0x12
#define OLATA           0x14

#define IODIRB          0x01
#define IPOLB           0x03
#define GPINTENB        0x05
#define DEFVALB         0x07
#define INTCONB         0x09
#define GPPUB           0x0D
#define INTFB           0x0F
#define INTCAPB         0x11
#define GPIOB           0x13
#define OLATB           0x15

// Bits in the IOCON register

#define IOCON_BANK_MODE 0x80
#define IOCON_MIRROR    0x40
#define IOCON_SEQOP     0x20
#define IOCON_DISSLW    0x10
#define IOCON_HAEN      0x08
#define IOCON_ODR       0x04
#define IOCON_INTPOL    0x02
#define IOCON_UNUSED    0x01

// Default initialisation mode

#define IOCON_INIT      (IOCON_SEQOP)

// GPIO Pins
#define	PIN1 (1 << 0)
#define	PIN2 (1 << 1)
#define	PIN3 (1 << 2)
#define	PIN4 (1 << 3)
#define	PIN5 (1 << 4)
#define	PIN6 (1 << 5)
#define	PIN7 (1 << 6)
#define	PIN8 (1 << 7)

// Something to output bit batters to screen
// exmaple: printf("input pins a "BYTETOBINARYPATTERN, BYTETOBINARY(single_byte_buffer));	
#define BYTETOBINARYPATTERN "%d%d%d%d%d%d%d%d\n"
#define BYTETOBINARY(byte)  \
  (byte & 0x80 ? 1 : 0), \
  (byte & 0x40 ? 1 : 0), \
  (byte & 0x20 ? 1 : 0), \
  (byte & 0x10 ? 1 : 0), \
  (byte & 0x08 ? 1 : 0), \
  (byte & 0x04 ? 1 : 0), \
  (byte & 0x02 ? 1 : 0), \
  (byte & 0x01 ? 1 : 0) 

// couple of macros to help bit flipping
#define SET_PIN_INPUT(x, y)	(x |= y)
#define SET_PIN_OUTPUT(x, y) (x &= (~y))
#define IS_SET(x,y) (x&y)


SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_switchpi_shutdown);
SWITCH_MODULE_RUNTIME_FUNCTION(mod_switchpi_runtime);
SWITCH_MODULE_LOAD_FUNCTION(mod_switchpi_load);
SWITCH_MODULE_DEFINITION(mod_switchpi, mod_switchpi_load, mod_switchpi_shutdown, mod_switchpi_runtime);

#define PD_QUEUE_LEN 200000

static struct {
	switch_mutex_t *mutex;
	int done;
	int main_running;
	switch_memory_pool_t *pool;
} globals;

SWITCH_MODULE_LOAD_FUNCTION(mod_switchpi_load)
{

	memset(&globals, 0, sizeof(globals));

	*module_interface = switch_loadable_module_create_module_interface(pool, modname);
	switch_mutex_init(&globals.mutex, SWITCH_MUTEX_NESTED, pool);

	globals.pool = pool;
	/* indicate that the module should continue to be loaded */
	return SWITCH_STATUS_SUCCESS;
}


SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_switchpi_shutdown)
{
	switch_mutex_lock(globals.mutex);
	globals.done = 1;
	globals.main_running = -1;
	switch_mutex_unlock(globals.mutex);

	while(globals.main_running) {
		switch_yield(500000);
	}

	return SWITCH_STATUS_SUCCESS;
}


SWITCH_MODULE_RUNTIME_FUNCTION(mod_switchpi_runtime)
{
	// char *dynamic_string = NULL;

	int q2w ;
	int foo = 0;
	int bar = 0;
	int oldfoo = 0;
// 	int shit = 0;
	int offhook = 0, calling = 0;// , answered = 0;
	int row = 0, col = 0;
	char output[3] = {0};

	char buf[32] = {0};
	char arg[64] = {0};
	int x = 0; // counter for buf
	char *uuid = NULL;
	char *pa_channel_id = NULL;
	

	char *key[4] = { "123", "456", "789", "*0#" };

	globals.main_running = 1;

	if ((q2w = wiringPiI2CSetup (0x24)) == -1) { 
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "q2w: Unable to initialise Keypad (I2C): %s\n", strerror (errno)); 
		return SWITCH_STATUS_FALSE;
	}

// Very simple direct control of the MCP23017:

	wiringPiI2CWriteReg8 (q2w, IOCON, IOCON_INIT) ;   // Initialize the 23017
	wiringPiI2CWriteReg8 (q2w, IODIRA, 0xF1);			// Sset Input Pins
	wiringPiI2CWriteReg8 (q2w, INTCONA, 0xF0);			// Enable Int Contol on Pins 5-8
	wiringPiI2CWriteReg8 (q2w, DEFVALA, 0xF0);		    // Set Default high for pins 5-8 low for 1-4
	wiringPiI2CWriteReg8 (q2w, GPINTENA, 0xF0);			// Enable Interrupt-On-Change on Pins 5-8
	wiringPiI2CWriteReg8 (q2w, GPPUA, 0x0F);			// Set Internal Pull Ups on pins 1-4 (external Used on 5-8)
	wiringPiI2CWriteReg8 (q2w, GPIOA, 0xF0);			// turn on output on Pins 5-8

	while(globals.main_running == 1) {
		foo = wiringPiI2CReadReg8 (q2w, GPIOA) ;		// Read Output
		if (oldfoo != foo) {							// if output has changed do something
			oldfoo = foo;								
			if (!IS_SET(foo, PIN1)) {
				if (offhook != 1) {
					switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"hook went offhook\n");
					switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"sending dial tone\n");
				}
				offhook = 1;
			} else {
				if (offhook != 0) {
					switch_stream_handle_t stream = { 0 };
					char *reply;
					SWITCH_STANDARD_STREAM(stream);
					switch_api_execute("pa", "hangup", NULL, &stream);
					reply = (char *) stream.data;
					switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "pa hangup reply [%s]\n", reply);
					switch_safe_free(stream.data);
					switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,"hook went onhook\n");
					// answered = 0;
					calling = 0;
					x = 0;
					memset(buf, 0, sizeof(buf));
					memset(arg, 0, sizeof(arg));
				}
				offhook = 0;
				
			}
			if (offhook) {
				if (!IS_SET(foo, PIN8)) row=1;				// Figure out the Row that was pressed
				else if (!IS_SET(foo, PIN7)) row=2;
				else if (!IS_SET(foo, PIN6)) row=3;
				else if (!IS_SET(foo, PIN5)) row=4;
				if (row > 0) {
					wiringPiI2CWriteReg8 (q2w, OLATA, 0x00) ;		// Clear latching
					wiringPiI2CWriteReg8 (q2w, IODIRA, 0X0F) ;		// Set Cols to Input, Rows to Output
					bar = wiringPiI2CReadReg8 (q2w, GPIOA) ;		// Read the Inputs
					if (!IS_SET(bar, PIN4)) col = 1;				// Figure out which Column
					if (!IS_SET(bar, PIN3)) col = 2;
					if (!IS_SET(bar, PIN2)) col = 3;
					wiringPiI2CWriteReg8 (q2w, IODIRA, 0XF1) ;		// reset cols to output and rows to input
				}
				if (row && col) {
					snprintf(output, 2, "%c", key[row-1][col-1]);  // convert Col/Row to standard DTMF pad chars
					switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "KEYPRESS [%s]\n", output);
					
					if (strcmp(output, "#") == 0) {
						switch_stream_handle_t stream = { 0 };

						char *reply;
						switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "make the call to [%s]\n", buf);
						SWITCH_STANDARD_STREAM(stream);
						snprintf(arg, sizeof(arg), "call %s", buf);
						switch_api_execute("pa", arg, NULL, &stream);
						reply = switch_strip_whitespace((char *) stream.data);
						switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "pa call reply [%s]\n", reply);
						if (!strncasecmp(reply, "success", 7)) {
							char *argv[4] = { 0 };
							int argc;
							switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "strncasecmp matched \n");
							if ((argc = switch_separate_string(reply, ':', argv, (sizeof(argv) / sizeof(argv[0]))))) {
								if (argc == 3) {
									pa_channel_id = argv[1];
									uuid = argv[2];
									switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "pa callid [%s] uuid [%s]\n", pa_channel_id, uuid);
								}
							}
						}

						
						calling = 1;
						// answered = 1;
					
						x = 0;
						memset(buf, 0, sizeof(buf));
						memset(arg, 0, sizeof(arg));
						switch_safe_free(stream.data);
					} else {
						if (!calling) {
							buf[x++] = key[row-1][col-1];
						} else {
							switch_stream_handle_t stream = { 0 };
							char *reply;
							buf[x] = key[row-1][col-1];
							SWITCH_STANDARD_STREAM(stream);
							snprintf(arg, sizeof(arg), "dtmf %s", buf);
							switch_api_execute("pa", arg, NULL, &stream);
							reply = switch_strip_whitespace((char *) stream.data);
							switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "pa dmtf reply [%s]\n", reply);
							switch_safe_free(stream.data);
						}
					}

					row = 0;										// Reset row and col to 0 for next time
					col = 0;
				}
			}
		}

		switch_yield(15000);
	
	}

	globals.main_running = 0;
	
	return SWITCH_STATUS_TERM;
}


/* For Emacs:
 * Local Variables:
 * mode:c
 * indent-tabs-mode:t
 * tab-width:4
 * c-basic-offset:4
 * End:
 * For VIM:
 * vim:set softtabstop=4 shiftwidth=4 tabstop=4 expandtab:
 */
