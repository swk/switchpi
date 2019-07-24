/*
 * keypad.c: quick keypad driver for i2c using wiringPi, MCP23017, and 
 * 4x3 matrix Keypad
 ***********************************************************************
 * Copyright (c) 2013-2019 Ken Rice <krice@freeswitch.org>
 *
 * This code is based on and requires WiringPi the work of Gordon Henderson
 * for more info visit https://projects.drogon.net/raspberry-pi/wiringpi/
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
 * The Original Code is keypad.c
 *
 * The Initial Developer of the Original Code is
 * Ken Rice <krice@freeswitch.org>
 * Portions created by the Initial Developer are Copyright (C)
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Ken Rice <krice@freeswitch.org>
 *
 *
 * keypad.c -- Main
 *
 *
 * to build:  gcc -o keypad -lwiringPi keypad.c
 * see http://switchpi.org for schematics.
 *
 */

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

/*
 *********************************************************************************
 * The works
 *********************************************************************************
 */

int main (int argc, char *argv [])
{
	int q2w ;
	int foo = 0;
	int bar = 0;
	int oldfoo = 0;
	int shit = 0;
	int row = 0, col = 0;
	char output[3] = {0};

	char *key[4] = { "123", "456", "789", "*0#" };

	setbuf(stdout, NULL); //diable stdout buffer

	if ((q2w = wiringPiI2CSetup (0x24)) == -1)		// 0x24 is the address of the MCP23017
		{ fprintf (stderr, "q2w: Unable to initialise I2C: %s\n", strerror (errno)) ; return 1 ; }

// Very simple direct control of the MCP23017:

	wiringPiI2CWriteReg8 (q2w, IOCON, IOCON_INIT) ;   // Initialize the 23017
	wiringPiI2CWriteReg8 (q2w, IODIRA, 0xF0);			// All On
	wiringPiI2CWriteReg8 (q2w, INTCONA, 0xF0);			// Enable Int Contol on Pins 5-8
	wiringPiI2CWriteReg8 (q2w, DEFVALA, 0xF0);		    // Set Default high for pins 5-8 low for 1-4
	wiringPiI2CWriteReg8 (q2w, GPINTENA, 0xF0);			// Enable Interrupt-On-Change on Pins 5-8
	wiringPiI2CWriteReg8 (q2w, GPPUA, 0x0F);			// Set Internal Pull Ups on pins 1-4 (external Used on 5-8)
	wiringPiI2CWriteReg8 (q2w, GPIOA, 0xF0);			// turn on output on Pins 5-8

	for (;;)
	{
		foo = wiringPiI2CReadReg8 (q2w, GPIOA) ;		// Read Output
		if (oldfoo != foo) {							// if output has changed do something
			oldfoo = foo;								

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
				wiringPiI2CWriteReg8 (q2w, IODIRA, 0XF0) ;		// reset cols to output and rows to input
			}
			if (row && col) {
				snprintf(output, 2, "%c", key[row-1][col-1]);  // convert Col/Row to standard DTMF pad chars
				printf("%s", output);
				if (strcmp(output, "#") == 0) printf("\n");		// if we got a # print a line feed
				row = 0;										// Reset row and col to 0 for next time
				col = 0;
			}
		}
	delay(15);	// delay 15ms... we dont need to hammer the bus
	}
  
	return 0 ;
}
