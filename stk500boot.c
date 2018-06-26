/*****************************************************************************
Title:     STK500v2 compatible bootloader
           Modified for Wiring board ATMega128-16MHz
Author:    Peter Fleury <pfleury@gmx.ch>   http://jump.to/fleury
Compiler:  avr-gcc 3.4.5 or 4.1 / avr-libc 1.4.3
Hardware:  All AVRs with bootloader support, tested with ATmega8
License:   GNU General Public License

Modified:  Worapoht Kornkaewwattanakul <dev@avride.com>   http://www.avride.com
Date:      17 October 2007
Update:    1st, 29 Dec 2007 : Enable CMD_SPI_MULTI but ignore unused command by return 0x00 byte response..
Compiler:  WINAVR20060421
Description: add timeout feature like previous Wiring bootloader

DESCRIPTION:
    This program allows an AVR with bootloader capabilities to
    read/write its own Flash/EEprom. To enter Programming mode
    an input pin is checked. If this pin is pulled low, programming mode
    is entered. If not, normal execution is done from $0000
    "reset" vector in Application area.
    Size fits into a 1024 word bootloader section
	when compiled with avr-gcc 4.1
	(direct replace on Wiring Board without fuse setting changed)

USAGE:
    - Set AVR MCU type and clock-frequency (F_CPU) in the Makefile.
    - Set baud rate below (AVRISP only works with 115200 bps)
    - compile/link the bootloader with the supplied Makefile
    - program the "Boot Flash section size" (BOOTSZ fuses),
      for boot-size 1024 words:  program BOOTSZ01
    - enable the BOOT Reset Vector (program BOOTRST)
    - Upload the hex file to the AVR using any ISP programmer
    - Program Boot Lock Mode 3 (program BootLock 11 and BootLock 12 lock bits) // (leave them)
    - Reset your AVR while keeping PROG_PIN pulled low // (for enter bootloader by switch)
    - Start AVRISP Programmer (AVRStudio/Tools/Program AVR)
    - AVRISP will detect the bootloader
    - Program your application FLASH file and optional EEPROM file using AVRISP

Note:
    Erasing the device without flashing, through AVRISP GUI button "Erase Device"
    is not implemented, due to AVRStudio limitations.
    Flash is always erased before programming.

	AVRdude:
	Please uncomment #define REMOVE_CMD_SPI_MULTI when using AVRdude.
	Comment #define REMOVE_PROGRAM_LOCK_BIT_SUPPORT to reduce code size
	Read Fuse Bits and Read/Write Lock Bits is not supported

NOTES:
    Based on Atmel Application Note AVR109 - Self-programming
    Based on Atmel Application Note AVR068 - STK500v2 Protocol

LICENSE:
    Copyright (C) 2006 Peter Fleury

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

*****************************************************************************/

//************************************************************************
//*	Edit History
//************************************************************************
//*	Jul  7,	2010	<MLS> = Mark Sproul msproul@skycharoit.com
//*	Jul  7,	2010	<MLS> Working on mega2560. No Auto-restart
//*	Jul  7,	2010	<MLS> Switched to 8K bytes (4K words) so that we have room for the monitor
//*	Jul  8,	2010	<MLS> Found older version of source that had auto restart, put that code back in
//*	Jul  8,	2010	<MLS> Adding monitor code
//*	Jul 11,	2010	<MLS> Added blinking LED while waiting for download to start
//*	Jul 11,	2010	<MLS> Added EEPROM test
//*	Jul 29,	2010	<MLS> Added recchar_timeout for timing out on bootloading
//*	Aug 23,	2010	<MLS> Added support for atmega2561
//*	Aug 26,	2010	<MLS> Removed support for BOOT_BY_SWITCH
//*	Sep  8,	2010	<MLS> Added support for atmega16
//*	Nov  9,	2010	<MLS> Issue 392:Fixed bug that 3 !!! in code would cause it to jump to monitor
//*	Jun 24,	2011	<MLS> Removed analogRead (was not used)
//*	Dec 29,	2011	<MLS> Issue 181: added watch dog timmer support
//*	Dec 29,	2011	<MLS> Issue 505:  bootloader is comparing the seqNum to 1 or the current sequence 
//*	Jan  1,	2012	<MLS> Issue 543: CMD_CHIP_ERASE_ISP now returns STATUS_CMD_FAILED instead of STATUS_CMD_OK
//*	Jan  1,	2012	<MLS> Issue 543: Write EEPROM now does something (NOT TESTED)
//*	Jan  1,	2012	<MLS> Issue 544: stk500v2 bootloader doesn't support reading fuses
//************************************************************************

//************************************************************************
//*	these are used to test issues
//*	http://code.google.com/p/arduino/issues/detail?id=505
//*	Reported by mark.stubbs, Mar 14, 2011
//*	The STK500V2 bootloader is comparing the seqNum to 1 or the current sequence 
//*	(IE: Requiring the sequence to be 1 or match seqNum before continuing).  
//*	The correct behavior is for the STK500V2 to accept the PC's sequence number, and echo it back for the reply message.
#define	_FIX_ISSUE_505_
//************************************************************************
//*	Issue 181: added watch dog timmer support
#define	_FIX_ISSUE_181_
// LCD startup screen and boot animation
#define LCD_HD44780
#define LCD_HD44780_ANIMATION
#define LCD_HD44780_COUNTER
// Dual serial support
#define DUALSERIAL
// EINSY board
#define EINSYBOARD


#include	<inttypes.h>
#include	<avr/io.h>
#include	<avr/interrupt.h>
#include	<avr/boot.h>
#include	<avr/pgmspace.h>
#include	<util/delay.h>
#include	<avr/eeprom.h>
#include	<avr/common.h>
#include	"command.h"

#ifdef LCD_HD44780
#include    "lcd.h"
#endif


#ifndef EEWE
	#define EEWE    1
#endif
#ifndef EEMWE
	#define EEMWE   2
#endif



/*
 * Uncomment the following lines to save code space
 */
//#define	REMOVE_PROGRAM_LOCK_BIT_SUPPORT		// disable program lock bits
//#define	REMOVE_BOOTLOADER_LED				// no LED to show active bootloader
//#define	REMOVE_CMD_SPI_MULTI				// disable processing of SPI_MULTI commands, Remark this line for AVRDUDE <Worapoht>
//



//************************************************************************
//*	LED on pin "PROGLED_PIN" on port "PROGLED_PORT"
//*	indicates that bootloader is active
//*	PG2 -> LED on Wiring board
//************************************************************************
#define		BLINK_LED_WHILE_WAITING

#ifdef _MEGA_BOARD_
	#define PROGLED_PORT	PORTB
	#define PROGLED_DDR		DDRB
	#define PROGLED_PIN		PINB7
#elif defined( _BOARD_AMBER128_ )
	//*	this is for the amber 128 http://www.soc-robotics.com/
	//*	onbarod led is PORTE4
	#define PROGLED_PORT	PORTD
	#define PROGLED_DDR		DDRD
	#define PROGLED_PIN		PINE7
#elif defined( _CEREBOTPLUS_BOARD_ ) || defined(_CEREBOT_II_BOARD_)
	//*	this is for the Cerebot 2560 board and the Cerebot-ii
	//*	onbarod leds are on PORTE4-7
	#define PROGLED_PORT	PORTE
	#define PROGLED_DDR		DDRE
	#define PROGLED_PIN		PINE7
#elif defined( _PENGUINO_ )
	//*	this is for the Penguino
	//*	onbarod led is PORTE4
	#define PROGLED_PORT	PORTC
	#define PROGLED_DDR		DDRC
	#define PROGLED_PIN		PINC6
#elif defined( _ANDROID_2561_ ) || defined( __AVR_ATmega2561__ )
	//*	this is for the Boston Android 2561
	//*	onbarod led is PORTE4
	#define PROGLED_PORT	PORTA
	#define PROGLED_DDR		DDRA
	#define PROGLED_PIN		PINA3
#elif defined( _BOARD_MEGA16 )
	//*	onbarod led is PORTA7
	#define PROGLED_PORT	PORTA
	#define PROGLED_DDR		DDRA
	#define PROGLED_PIN		PINA7
	#define UART_BAUDRATE_DOUBLE_SPEED 0

#elif defined( _BOARD_BAHBOT_ )
	//*	dosent have an onboard LED but this is what will probably be added to this port
	#define PROGLED_PORT	PORTB
	#define PROGLED_DDR		DDRB
	#define PROGLED_PIN		PINB0

#elif defined( _BOARD_ROBOTX_ )
	#define PROGLED_PORT	PORTB
	#define PROGLED_DDR		DDRB
	#define PROGLED_PIN		PINB6
#elif defined( _BOARD_CUSTOM1284_BLINK_B0_ )
	#define PROGLED_PORT	PORTB
	#define PROGLED_DDR		DDRB
	#define PROGLED_PIN		PINB0
#elif defined( _BOARD_CUSTOM1284_ )
	#define PROGLED_PORT	PORTD
	#define PROGLED_DDR		DDRD
	#define PROGLED_PIN		PIND5
#elif defined( _AVRLIP_ )
	#define PROGLED_PORT	PORTB
	#define PROGLED_DDR		DDRB
	#define PROGLED_PIN		PINB5
#elif defined( _BOARD_STK500_ )
	#define PROGLED_PORT	PORTA
	#define PROGLED_DDR		DDRA
	#define PROGLED_PIN		PINA7
#elif defined( _BOARD_STK502_ )
	#define PROGLED_PORT	PORTB
	#define PROGLED_DDR		DDRB
	#define PROGLED_PIN		PINB5
#elif defined( _BOARD_STK525_ )
	#define PROGLED_PORT	PORTB
	#define PROGLED_DDR		DDRB
	#define PROGLED_PIN		PINB7
#else
	#define PROGLED_PORT	PORTG
	#define PROGLED_DDR		DDRG
	#define PROGLED_PIN		PING2
#endif



/*
 * define CPU frequency in Mhz here if not defined in Makefile
 */
#ifndef F_CPU
	#define F_CPU 16000000UL
#endif

#define	_BLINK_LOOP_COUNT_	(F_CPU / 2250)
/*
 * UART Baudrate, AVRStudio AVRISP only accepts 115200 bps
 */

#ifndef BAUDRATE
	#define BAUDRATE 115200
#endif

/*
 *  Enable (1) or disable (0) USART double speed operation
 */
#ifndef UART_BAUDRATE_DOUBLE_SPEED
	#if defined (__AVR_ATmega32__)
		#define UART_BAUDRATE_DOUBLE_SPEED 0
	#else
		#define UART_BAUDRATE_DOUBLE_SPEED 1
	#endif
#endif

/*
 * HW and SW version, reported to AVRISP, must match version of AVRStudio
 */
#define CONFIG_PARAM_BUILD_NUMBER_LOW	0
#define CONFIG_PARAM_BUILD_NUMBER_HIGH	0
#define CONFIG_PARAM_HW_VER				0x0F
#define CONFIG_PARAM_SW_MAJOR			2
#define CONFIG_PARAM_SW_MINOR			0x0A

/*
 * Calculate the address where the bootloader starts from FLASHEND and BOOTSIZE
 * (adjust BOOTSIZE below and BOOTLOADER_ADDRESS in Makefile if you want to change the size of the bootloader)
 */
//#define BOOTSIZE 1024
#if FLASHEND > 0x0F000
	#define BOOTSIZE 8192
#else
	#define BOOTSIZE 2048
#endif

//#define APP_END  (FLASHEND -(2*BOOTSIZE) + 1)
#define APP_END  (FLASHEND -(BOOTSIZE) + 1)

/*
 * Signature bytes are not available in avr-gcc io_xxx.h
 */
#if defined (__AVR_ATmega8__)
	#define SIGNATURE_BYTES 0x1E9307
#elif defined (__AVR_ATmega16__)
	#define SIGNATURE_BYTES 0x1E9403
#elif defined (__AVR_ATmega32__)
	#define SIGNATURE_BYTES 0x1E9502
#elif defined (__AVR_ATmega8515__)
	#define SIGNATURE_BYTES 0x1E9306
#elif defined (__AVR_ATmega8535__)
	#define SIGNATURE_BYTES 0x1E9308
#elif defined (__AVR_ATmega162__)
	#define SIGNATURE_BYTES 0x1E9404
#elif defined (__AVR_ATmega128__)
	#define SIGNATURE_BYTES 0x1E9702
#elif defined (__AVR_ATmega1280__)
	#define SIGNATURE_BYTES 0x1E9703
#elif defined (__AVR_ATmega2560__)
	#define SIGNATURE_BYTES 0x1E9801
#elif defined (__AVR_ATmega2561__)
	#define SIGNATURE_BYTES 0x1e9802
#elif defined (__AVR_ATmega1284P__)
	#define SIGNATURE_BYTES 0x1e9705
#elif defined (__AVR_ATmega640__)
	#define SIGNATURE_BYTES  0x1e9608
#elif defined (__AVR_ATmega64__)
	#define SIGNATURE_BYTES  0x1E9602
#elif defined (__AVR_ATmega169__)
	#define SIGNATURE_BYTES  0x1e9405
#elif defined (__AVR_AT90USB1287__)
	#define SIGNATURE_BYTES  0x1e9782
#else
	#error "no signature definition for MCU available"
#endif


#if defined(_BOARD_ROBOTX_) || defined(__AVR_AT90USB1287__) || defined(__AVR_AT90USB1286__)
	#define	UART_BAUD_RATE_LOW			UBRR1L
	#define	UART_STATUS_REG				UCSR1A
	#define	UART_CONTROL_REG			UCSR1B
	#define	UART_ENABLE_TRANSMITTER		TXEN1
	#define	UART_ENABLE_RECEIVER		RXEN1
	#define	UART_TRANSMIT_COMPLETE		TXC1
	#define	UART_RECEIVE_COMPLETE		RXC1
	#define	UART_DATA_REG				UDR1
	#define	UART_DOUBLE_SPEED			U2X1

#elif defined(__AVR_ATmega8__) || defined(__AVR_ATmega16__) || defined(__AVR_ATmega32__) \
	|| defined(__AVR_ATmega8515__) || defined(__AVR_ATmega8535__)
	/* ATMega8 with one USART */
	#define	UART_BAUD_RATE_LOW			UBRRL
	#define	UART_STATUS_REG				UCSRA
	#define	UART_CONTROL_REG			UCSRB
	#define	UART_ENABLE_TRANSMITTER		TXEN
	#define	UART_ENABLE_RECEIVER		RXEN
	#define	UART_TRANSMIT_COMPLETE		TXC
	#define	UART_RECEIVE_COMPLETE		RXC
	#define	UART_DATA_REG				UDR
	#define	UART_DOUBLE_SPEED			U2X

#elif defined(__AVR_ATmega64__) || defined(__AVR_ATmega128__) || defined(__AVR_ATmega162__) \
	 || defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__) || defined(__AVR_ATmega2561__)
	/* ATMega with two USART, use UART0 */
	#define	UART_BAUD_RATE_LOW			UBRR0L
	#define	UART_STATUS_REG				UCSR0A
	#define	UART_CONTROL_REG			UCSR0B
	#define	UART_ENABLE_TRANSMITTER		TXEN0
	#define	UART_ENABLE_RECEIVER		RXEN0
	#define	UART_TRANSMIT_COMPLETE		TXC0
	#define	UART_RECEIVE_COMPLETE		RXC0
	#define	UART_DATA_REG				UDR0
	#define	UART_DOUBLE_SPEED			U2X0
#elif defined(UBRR0L) && defined(UCSR0A) && defined(TXEN0)
	/* ATMega with two USART, use UART0 */
	#define	UART_BAUD_RATE_LOW			UBRR0L
	#define	UART_STATUS_REG				UCSR0A
	#define	UART_CONTROL_REG			UCSR0B
	#define	UART_ENABLE_TRANSMITTER		TXEN0
	#define	UART_ENABLE_RECEIVER		RXEN0
	#define	UART_TRANSMIT_COMPLETE		TXC0
	#define	UART_RECEIVE_COMPLETE		RXC0
	#define	UART_DATA_REG				UDR0
	#define	UART_DOUBLE_SPEED			U2X0
#elif defined(UBRRL) && defined(UCSRA) && defined(UCSRB) && defined(TXEN) && defined(RXEN)
	//* catch all
	#define	UART_BAUD_RATE_LOW			UBRRL
	#define	UART_STATUS_REG				UCSRA
	#define	UART_CONTROL_REG			UCSRB
	#define	UART_ENABLE_TRANSMITTER		TXEN
	#define	UART_ENABLE_RECEIVER		RXEN
	#define	UART_TRANSMIT_COMPLETE		TXC
	#define	UART_RECEIVE_COMPLETE		RXC
	#define	UART_DATA_REG				UDR
	#define	UART_DOUBLE_SPEED			U2X
#else
	#error "no UART definition for MCU available"
#endif


/*
 * Macro to calculate UBBR from XTAL and baudrate
 */
#if defined(__AVR_ATmega32__) && UART_BAUDRATE_DOUBLE_SPEED
	#define UART_BAUD_SELECT(baudRate,xtalCpu) ((xtalCpu / 4 / baudRate - 1) / 2)
#elif defined(__AVR_ATmega32__)
	#define UART_BAUD_SELECT(baudRate,xtalCpu) ((xtalCpu / 8 / baudRate - 1) / 2)
#elif UART_BAUDRATE_DOUBLE_SPEED
	#define UART_BAUD_SELECT(baudRate,xtalCpu) (((float)(xtalCpu))/(((float)(baudRate))*8.0)-1.0+0.5)
#else
	#define UART_BAUD_SELECT(baudRate,xtalCpu) (((float)(xtalCpu))/(((float)(baudRate))*16.0)-1.0+0.5)
#endif

#ifdef DUALSERIAL

// UART defines
#define	UART_BAUD_RATE_LOW0			UBRR0L
#define	UART_STATUS_REG0			UCSR0A
#define	UART_CONTROL_REG0			UCSR0B
#define	UART_ENABLE_TRANSMITTER0	TXEN0
#define	UART_ENABLE_RECEIVER0		RXEN0
#define	UART_TRANSMIT_COMPLETE0		TXC0
#define	UART_RECEIVE_COMPLETE0		RXC0
#define	UART_DATA_REG0				UDR0
#define	UART_DOUBLE_SPEED0			U2X0

#define	UART_BAUD_RATE_LOW2			UBRR1L
#define	UART_STATUS_REG2			UCSR1A
#define	UART_CONTROL_REG2			UCSR1B
#define	UART_ENABLE_TRANSMITTER2	TXEN1
#define	UART_ENABLE_RECEIVER2		RXEN1
#define	UART_TRANSMIT_COMPLETE2		TXC1
#define	UART_RECEIVE_COMPLETE2		RXC1
#define	UART_DATA_REG2				UDR1
#define	UART_DOUBLE_SPEED2			U2X1

#endif //DUALSERIAL


#define UART_BAUD_SELECT(baudRate,xtalCpu) (((float)(xtalCpu))/(((float)(baudRate))*8.0)-1.0+0.5)

/*
 * States used in the receive state machine
 */
#define	ST_START		0
#define	ST_GET_SEQ_NUM	1
#define ST_MSG_SIZE_1	2
#define ST_MSG_SIZE_2	3
#define ST_GET_TOKEN	4
#define ST_GET_DATA		5
#define	ST_GET_CHECK	6
#define	ST_PROCESS		7

/*
 * use 16bit address variable for ATmegas with <= 64K flash
 */
#if defined(RAMPZ)
	typedef uint32_t address_t;
#else
	typedef uint16_t address_t;
#endif

/*
 * function prototypes
 */
static void sendchar(char c);
//static unsigned char recchar(void);

#ifdef DUALSERIAL
int selectedSerial;
#endif //DUALSERIAL

/*
 * since this bootloader is not linked against the avr-gcc crt1 functions,
 * to reduce the code size, we need to provide our own initialization
 */
void __jumpMain	(void) __attribute__ ((naked)) __attribute__ ((section (".init9")));
#include <avr/sfr_defs.h>

//#define	SPH_REG	0x3E
//#define	SPL_REG	0x3D

#define STACK_TOP (RAMEND - 16)

//*****************************************************************************
void __jumpMain(void)
{
//*	July 17, 2010	<MLS> Added stack pointer initialzation
//*	the first line did not do the job on the ATmega128

	asm volatile ( ".set __stack, %0" :: "i" (STACK_TOP) );

//*	set stack pointer to top of RAM

	asm volatile ( "ldi	16, %0" :: "i" (STACK_TOP >> 8) );
	asm volatile ( "out %0,16" :: "i" (AVR_STACK_POINTER_HI_ADDR) );

	asm volatile ( "ldi	16, %0" :: "i" (STACK_TOP & 0x0ff) );
	asm volatile ( "out %0,16" :: "i" (AVR_STACK_POINTER_LO_ADDR) );

	asm volatile ( "clr __zero_reg__" );									// GCC depends on register r1 set to 0
	asm volatile ( "out %0, __zero_reg__" :: "I" (_SFR_IO_ADDR(SREG)) );	// set SREG to 0
	asm volatile ( "jmp main");												// jump to main()
}


//*****************************************************************************
void delay_ms(unsigned int timedelay)
{
	unsigned int i;
	for (i=0;i<timedelay;i++)
	{
		_delay_ms(0.5);
	}
}
#if 0
void lcd_print_hex_nibble(uint8_t val)
{
	lcd_putc((val > 9)?('A' + val - 10):('0' + val));
}

void lcd_print_hex_byte(uint8_t val)
{
	lcd_print_hex_nibble(val >> 4);
	lcd_print_hex_nibble(val & 0xf);
}

void lcd_print_hex_word(uint16_t val)
{
	lcd_print_hex_byte(val >> 8);
	lcd_print_hex_byte(val & 0xff);
}

void lcd_print_hex_dword(uint32_t val)
{
	lcd_print_hex_word(val >> 16);
	lcd_print_hex_word(val & 0xffff);
}

const unsigned long ulFlashEnd = FLASHEND;
const unsigned long ulRamEnd = RAMEND;
const unsigned long ulBootSize = BOOTSIZE;
const unsigned long ulAppEnd = APP_END;
#endif //if 0
//*****************************************************************************
/*
 * send single byte to USART, wait until transmission is completed
 */
static void sendchar(char c)
{
#ifdef DUALSERIAL
	if (selectedSerial == 0)
	{
		UART_DATA_REG0 = c;												// prepare transmission
		while (!(UART_STATUS_REG0 & (1 << UART_TRANSMIT_COMPLETE0)));	// wait until byte sent
		UART_STATUS_REG0 |= (1 << UART_TRANSMIT_COMPLETE0);				// delete TXCflag
	}
	else if (selectedSerial == 2)
	{
		UART_DATA_REG2 = c;												// prepare transmission
		while (!(UART_STATUS_REG2 & (1 << UART_TRANSMIT_COMPLETE2)));	// wait until byte sent
		UART_STATUS_REG2 |= (1 << UART_TRANSMIT_COMPLETE2);				// delete TXCflag
	}
#else //DUALSERIAL
	UART_DATA_REG	=	c;										// prepare transmission
	while (!(UART_STATUS_REG & (1 << UART_TRANSMIT_COMPLETE)));	// wait until byte sent
	UART_STATUS_REG |= (1 << UART_TRANSMIT_COMPLETE);			// delete TXCflag
#endif //DUALSERIAL
}


//************************************************************************
#ifdef DUALSERIAL
static int Serial_Available(int serial)
{
	if (serial == 0)
		return (UART_STATUS_REG0 & (1 << UART_RECEIVE_COMPLETE0));	// wait for data
	else if (serial == 2)
		return (UART_STATUS_REG2 & (1 << UART_RECEIVE_COMPLETE2));	// wait for data
	return 0;
}
#else //DUALSERIAL
static int	Serial_Available(void)
{
	return (UART_STATUS_REG & (1 << UART_RECEIVE_COMPLETE));	// wait for data
}
#endif //DUALSERIAL


//*****************************************************************************
/*
 * Read single byte from USART, block if no data available
 */
/*static unsigned char recchar(void)
{
#ifdef DUALSERIAL
	if (selectedSerial == 0)
	{
		while (!(UART_STATUS_REG0 & (1 << UART_RECEIVE_COMPLETE0))) { } // wait for data
		return UART_DATA_REG0;
	}
	else if (selectedSerial == 2)
	{
		while (!(UART_STATUS_REG2 & (1 << UART_RECEIVE_COMPLETE2))) { } // wait for data
		return UART_DATA_REG2;
	}
	return 0;
#else //DUALSERIAL
	while (!(UART_STATUS_REG & (1 << UART_RECEIVE_COMPLETE))) { } // wait for data
	return UART_DATA_REG;
#endif //DUALSERIAL
}*/

#define	MAX_TIME_COUNT	(F_CPU >> 1)
//*****************************************************************************
static unsigned char recchar_timeout(void)
{
	uint32_t count = 0;
#ifdef DUALSERIAL
	while (1)
	{
		if ((selectedSerial == 0) && (UART_STATUS_REG0 & (1 << UART_RECEIVE_COMPLETE0))) break;
		else if ((selectedSerial == 2) && (UART_STATUS_REG2 & (1 << UART_RECEIVE_COMPLETE2))) break;
		count++;
		if (count > MAX_TIME_COUNT)
		{
		unsigned int	data;
		#if (FLASHEND > 0x10000)
			data	=	pgm_read_word_far(0);	//*	get the first word of the user program
		#else
			data	=	pgm_read_word_near(0);	//*	get the first word of the user program
		#endif
			if (data != 0xffff)					//*	make sure its valid before jumping to it.
			{
				asm volatile(
						"clr	r30		\n\t"
						"clr	r31		\n\t"
						"ijmp	\n\t"
						);
			}
			count	=	0;
		}
	}
	if (selectedSerial == 0) return UART_DATA_REG0;
	else if (selectedSerial == 2) return UART_DATA_REG2;
	return 0;
#else //DUALSERIAL
	while (!(UART_STATUS_REG & (1 << UART_RECEIVE_COMPLETE)))
	{
		// wait for data
		count++;
		if (count > MAX_TIME_COUNT)
		{
		unsigned int	data;
		#if (FLASHEND > 0x10000)
			data	=	pgm_read_word_far(0);	//*	get the first word of the user program
		#else
			data	=	pgm_read_word_near(0);	//*	get the first word of the user program
		#endif
			if (data != 0xffff)					//*	make sure its valid before jumping to it.
			{
				asm volatile(
						"clr	r30		\n\t"
						"clr	r31		\n\t"
						"ijmp	\n\t"
						);
			}
			count	=	0;
		}
	}
	return UART_DATA_REG;
#endif //DUALSERIAL
}

#ifdef DUALSERIAL
void initUart(void)
{
	// init uart0
	UART_STATUS_REG0	|=	(1 <<UART_DOUBLE_SPEED0);
	UART_BAUD_RATE_LOW0	=	UART_BAUD_SELECT(BAUDRATE,F_CPU);
	UART_CONTROL_REG0	=	(1 << UART_ENABLE_RECEIVER0) | (1 << UART_ENABLE_TRANSMITTER0);
	// init uart2
	UART_STATUS_REG2	|=	(1 <<UART_DOUBLE_SPEED2);
	UART_BAUD_RATE_LOW2	=	UART_BAUD_SELECT(BAUDRATE,F_CPU);
	UART_CONTROL_REG2	=	(1 << UART_ENABLE_RECEIVER2) | (1 << UART_ENABLE_TRANSMITTER2);
}
#endif //DUALSERIAL

/*void sendHello() {
	sendchar('H');
	sendchar('e');
	sendchar('l');
	sendchar('l');
	sendchar('o');
	sendchar('!');
	sendchar('\n');
}*/

#ifdef EINSYBOARD

void blinkBootLed(int state)
{
    if (state == 1)
        PORTB = 0b10000000;
    else
        PORTB = 0b00000000;
}

//Heaters off (PG5=0, PE5=0)
//Fans on (PH5=1, PH3=1)
//Motors off (PA4..7=1)
void pinsToDefaultState(void)
{
/*
    DDRG = 0b00001000;
    DDRE = 0b00001000;
    DDRH = 0b00101000;
    DDRA = 0b00011110;
    DDRB = 0b10000000;
    PORTH = 0b00101000;
    PORTG = 0b00000000;
    PORTE = 0b00000000;
    PORTA = 0b00000000;*/ //original code
	DDRA |= 0b11110000; //PA4..7 out
	PORTA |= 0b11110000; //PA4..7 = 1
	DDRE |= 0b00100000; //PE5 out
	PORTE &= 0b11011111; //PE5 = 0
	DDRG |= 0b00100000; //PG5 out
	PORTG &= 0b11011111; //PG5 = 0
	DDRH |= 0b00101000; //PH5, PH3 out
	PORTH |= 0b00101000; //PH5, PH3 = 1
}

#endif //EINSYBOARD

//*	for watch dog timer startup
//void (*app_start)(void) = 0x0000;

	unsigned long flashSize = 0; //flash data size in bytes
	unsigned long flashCounter = 0; //flash counter (readed / written bytes)
	address_t flashAddressLast = 0; //last written flash address
	int flashOperation = 0; //current flash operation (0-nothing, 1-write, 2-verify)

#define RAMSIZE        0x2000
#define boot_src_addr  (*((uint32_t*)(RAMSIZE - 16)))
#define boot_dst_addr  (*((uint32_t*)(RAMSIZE - 12)))
#define boot_copy_size (*((uint16_t*)(RAMSIZE - 8)))
#define boot_reserved  (*((uint8_t*)(RAMSIZE - 6)))
#define boot_app_flags (*((uint8_t*)(RAMSIZE - 5)))
#define boot_app_magic (*((uint32_t*)(RAMSIZE - 4)))
#define BOOT_APP_FLG_ERASE 0x01
#define BOOT_APP_FLG_COPY  0x02
#define BOOT_APP_FLG_FLASH 0x04
#define BOOT_APP_FLG_RUN   0x08 //!< Do not jump to application immediately
	

//*****************************************************************************
int main(void)
{
	address_t		address			=	0;
	address_t		eraseAddress	=	0;
	unsigned char	msgParseState;
	unsigned int	ii				=	0;
	unsigned char	checksum		=	0;
	unsigned char	seqNum			=	0;
	unsigned int	msgLength		=	0;
	unsigned char	msgBuffer[285];
	unsigned char	c, *p;
	unsigned char   isLeave = 0;

	unsigned long	boot_timeout = 20000ul; // should be about 1 second
	unsigned long	boot_timer   =   0;
	unsigned int	boot_state  =   0;

	//*	some chips dont set the stack properly
// this is already done in __jumpMain
/*	asm volatile ( ".set __stack, %0" :: "i" (RAMEND) );
	asm volatile ( "ldi	16, %0" :: "i" (RAMEND >> 8) );
	asm volatile ( "out %0,16" :: "i" (AVR_STACK_POINTER_HI_ADDR) );
	asm volatile ( "ldi	16, %0" :: "i" (RAMEND & 0x0ff) );
	asm volatile ( "out %0,16" :: "i" (AVR_STACK_POINTER_LO_ADDR) );*/ 

#ifdef _FIX_ISSUE_181_
	//************************************************************************
	//*	Dec 29,	2011	<MLS> Issue #181, added watch dog timmer support
	//*	handle the watch dog timer
	uint8_t	mcuStatusReg;
	mcuStatusReg	=	MCUSR;

	__asm__ __volatile__ ("cli");
	__asm__ __volatile__ ("wdr");
	MCUSR	=	0;
	WDTCSR	|=	_BV(WDCE) | _BV(WDE);
	WDTCSR	=	0;
	__asm__ __volatile__ ("sei");
	// check if WDT generated the reset, if so, go straight to app
	if (mcuStatusReg & _BV(WDRF))
	{
		if (boot_app_magic == 0x55aa55aa)
		{
            if (boot_app_flags & BOOT_APP_FLG_RUN)goto start;

			address = boot_dst_addr;
			address_t pageAddress = address;
			while (boot_copy_size)
			{
				if (boot_app_flags & BOOT_APP_FLG_ERASE)
				{
					boot_page_erase(pageAddress);
					boot_spm_busy_wait();
				}
				pageAddress += SPM_PAGESIZE;
				if ((boot_app_flags & BOOT_APP_FLG_COPY))
				{
					while (boot_copy_size && (address < pageAddress))
					{
						uint16_t word = 0x0000;
						if (boot_app_flags & BOOT_APP_FLG_FLASH)
							word = pgm_read_word_far(boot_src_addr); //from FLASH
						else
							word = *((uint16_t*)boot_src_addr); //from RAM
						boot_page_fill(address, word);
						address	+= 2;
						boot_src_addr += 2;
						if (boot_copy_size > 2)
							boot_copy_size -= 2;
						else
							boot_copy_size = 0;
					}
					boot_page_write(pageAddress - SPM_PAGESIZE);
					boot_spm_busy_wait();
					boot_rww_enable();
				}
				else
				{
					address	+= SPM_PAGESIZE;
					if (boot_copy_size > SPM_PAGESIZE)
						boot_copy_size -= SPM_PAGESIZE;
					else
						boot_copy_size = 0;
				}
			}
///			boot_copy_size = tmp_boot_copy_size;
///			boot_src_addr = tmp_boot_src_addr;
		}
		goto exit;
// original implementation app_start() does not work
//		app_start();
	}
	start:
	//************************************************************************
#endif


	/*
	 * Branch to bootloader or application code ?
	 */

#ifdef DUALSERIAL
	selectedSerial = 0;
#endif //DUALSERIAL


#ifndef REMOVE_BOOTLOADER_LED
	/* PROG_PIN pulled low, indicate with LED that bootloader is active */
	PROGLED_DDR		|=	(1<<PROGLED_PIN);
//	PROGLED_PORT	&=	~(1<<PROGLED_PIN);	// active low LED ON
	PROGLED_PORT	|=	(1<<PROGLED_PIN);	// active high LED ON


#endif
	/*
	 * Init UART
	 * set baudrate and enable USART receiver and transmiter without interrupts
	 */
#if UART_BAUDRATE_DOUBLE_SPEED
	UART_STATUS_REG		|=	(1 <<UART_DOUBLE_SPEED);
#endif
	UART_BAUD_RATE_LOW	=	UART_BAUD_SELECT(BAUDRATE,F_CPU);
	UART_CONTROL_REG	=	(1 << UART_ENABLE_RECEIVER) | (1 << UART_ENABLE_TRANSMITTER);

	asm volatile ("nop");			// wait until port has changed

#ifdef EINSYBOARD
    pinsToDefaultState();
    blinkBootLed(1);
#endif //EINSYBOARD

#ifdef DUALSERIAL
    initUart();
#endif //DUALSERIAL

#ifdef LCD_HD44780
    lcd_init();
    lcd_clrscr();
    lcd_goto(0);
/*	if (boot_app_magic == 0x55aa55aa)
	{
		lcd_print_hex_dword(boot_src_addr);
		lcd_putc(' ');
		lcd_print_hex_dword(boot_dst_addr);
	    lcd_goto(42);
		lcd_print_hex_word(boot_copy_size);
		lcd_putc(' ');
		lcd_print_hex_word(boot_app_flags);
		lcd_putc(' ');
		lcd_print_hex_dword(boot_app_magic);
		boot_app_magic = 0x00000000;
		while(1);
	}*/
/*	lcd_puts("B");
    lcd_goto(21);
    lcd_puts("Original Prusa i3");
    lcd_goto(47);
    lcd_puts("Prusa Research");*/
    lcd_goto(65);
    lcd_puts("Original Prusa i3");
    lcd_goto(23);
    lcd_puts("Prusa Research");
//    lcd_goto(90);
//	lcd_puts("boot...    ...");
    lcd_goto(101);
	lcd_puts("...");

#endif //LCD_HD44780


#ifdef EINSYBOARD
    blinkBootLed(0);
#endif //EINSYBOARD

    uint16_t animationTimer = 0;
    uint16_t animationFrame = 0;


	while (boot_state==0)
	{
#ifdef DUALSERIAL
		while ((!(Serial_Available(0))) && (!(Serial_Available(2))) && (boot_state == 0))		// wait for data
		{
			_delay_ms(0.001);
			boot_timer++;
			if (boot_timer > boot_timeout)
			{
				boot_state	=	1; // (after ++ -> boot_state=2 bootloader timeout, jump to main 0x00000 )
			}
		#ifdef BLINK_LED_WHILE_WAITING
			if ((boot_timer % _BLINK_LOOP_COUNT_) == 0)
			{
				//*	toggle the LED
				PROGLED_PORT	^=	(1<<PROGLED_PIN);	// turn LED ON
			}
		#endif
		}
		if (Serial_Available(2))
			selectedSerial = 2;
#else //DUALSERIAL
		while ((!(Serial_Available())) && (boot_state == 0))		// wait for data
		{
			_delay_ms(0.001);
			boot_timer++;
			if (boot_timer > boot_timeout)
			{
				boot_state	=	1; // (after ++ -> boot_state=2 bootloader timeout, jump to main 0x00000 )
			}
		#ifdef BLINK_LED_WHILE_WAITING
			if ((boot_timer % _BLINK_LOOP_COUNT_) == 0)
			{
				//*	toggle the LED
				PROGLED_PORT	^=	(1<<PROGLED_PIN);	// turn LED ON
			}
		#endif
		}
#endif //DUALSERIAL
		boot_state++; // ( if boot_state=1 bootloader received byte from UART, enter bootloader mode)
	}

    int messageShown = 0;

	if (boot_state==1)
	{
		//*	main loop
		while (!isLeave)
		{
			/*
			 * Collect received bytes to a complete message
			 */
			msgParseState	=	ST_START;
			while ( msgParseState != ST_PROCESS )
			{
				if (boot_state==1)
				{
					boot_state	=	0;
					c			=	UART_DATA_REG;
				}
				else
				{
				//	c	=	recchar();
					c	=	recchar_timeout();
					
				}


				switch (msgParseState)
				{
					case ST_START:
						if ( c == MESSAGE_START )
						{
							msgParseState	=	ST_GET_SEQ_NUM;
							checksum		=	MESSAGE_START^0;
						}
						break;

					case ST_GET_SEQ_NUM:
					#ifdef _FIX_ISSUE_505_
						seqNum			=	c;
						msgParseState	=	ST_MSG_SIZE_1;
						checksum		^=	c;
					#else
						if ( (c == 1) || (c == seqNum) )
						{
							seqNum			=	c;
							msgParseState	=	ST_MSG_SIZE_1;
							checksum		^=	c;
						}
						else
						{
							msgParseState	=	ST_START;
						}
					#endif
						break;

					case ST_MSG_SIZE_1:
						msgLength		=	c<<8;
						msgParseState	=	ST_MSG_SIZE_2;
						checksum		^=	c;
						break;

					case ST_MSG_SIZE_2:
						msgLength		|=	c;
						msgParseState	=	ST_GET_TOKEN;
						checksum		^=	c;
						break;

					case ST_GET_TOKEN:
						if ( c == TOKEN )
						{
							msgParseState	=	ST_GET_DATA;
							checksum		^=	c;
							ii				=	0;
						}
						else
						{
							msgParseState	=	ST_START;
						}
						break;

					case ST_GET_DATA:
						msgBuffer[ii++]	=	c;
						checksum		^=	c;
						if (ii == msgLength )
						{
							msgParseState	=	ST_GET_CHECK;
						}
						break;

					case ST_GET_CHECK:
						if ( c == checksum )
						{
							msgParseState	=	ST_PROCESS;
						}
						else
						{
							msgParseState	=	ST_START;
						}
						break;
				}	//	switch
			}	//	while(msgParseState)

#ifdef LCD_HD44780
            if (messageShown == 0)
			{
			    lcd_clrscr();
                lcd_goto(20);
                lcd_puts(" Do not disconnect!");
                lcd_goto(45);
                lcd_puts(" Upgrading firmware");
                messageShown = 1;
            }
#endif //LCD_HD44780

#ifdef LCD_HD44780_ANIMATION
			if (flashSize == 0)
			{
				animationTimer++;
				if (animationTimer > 10)
				{
					animationTimer = 0;
					animationFrame++;
					if (animationFrame > 5) animationFrame = 0;
					lcd_goto(91);
					lcd_puts("|    |");
					lcd_goto((animationFrame <= 3)?(92 + animationFrame):(98 - animationFrame));
					lcd_putc('*');
				}
			}
#endif //LCD_HD44780_ANIMATION

#ifdef LCD_HD44780_COUNTER
			if ((flashSize != 0) && flashOperation)
			{
				if (flashOperation == 1) //write
				{
					lcd_goto(88);
					lcd_puts("write ");
				}
				if (flashOperation == 2) //verify
				{
					lcd_goto(87);
					lcd_puts("verify ");
				}
				int progress = 100 * flashCounter / flashSize;
				char text[4] = "   ";
				for (int i = 2; i >= 0; i--)
					if (progress > 0)
					{
						text[i] = '0' + (progress % 10);
						progress /= 10;
					}
					else
						text[i] = ' ';
				lcd_puts(text);
				lcd_putc('%');
			}
#endif //LCD_HD44780_COUNTER

			/*
			 * Now process the STK500 commands, see Atmel Appnote AVR068
			 */

			switch (msgBuffer[0])
			{
	#ifndef REMOVE_CMD_SPI_MULTI
				case CMD_SPI_MULTI:
					{
						unsigned char answerByte;
						unsigned char flag=0;

						if ( msgBuffer[4]== 0x30 )
						{
							unsigned char signatureIndex	=	msgBuffer[6];

							if ( signatureIndex == 0 )
							{
								answerByte	=	(SIGNATURE_BYTES >> 16) & 0x000000FF;
							}
							else if ( signatureIndex == 1 )
							{
								answerByte	=	(SIGNATURE_BYTES >> 8) & 0x000000FF;
							}
							else
							{
								answerByte	=	SIGNATURE_BYTES & 0x000000FF;
							}
						}
						else if ( msgBuffer[4] & 0x50 )
						{
						//*	Issue 544: 	stk500v2 bootloader doesn't support reading fuses
						//*	I cant find the docs that say what these are supposed to be but this was figured out by trial and error
						//	answerByte	=	boot_lock_fuse_bits_get(GET_LOW_FUSE_BITS);
						//	answerByte	=	boot_lock_fuse_bits_get(GET_HIGH_FUSE_BITS);
						//	answerByte	=	boot_lock_fuse_bits_get(GET_EXTENDED_FUSE_BITS);
							if (msgBuffer[4] == 0x50)
							{
								answerByte	=	boot_lock_fuse_bits_get(GET_LOW_FUSE_BITS);
							}
							else if (msgBuffer[4] == 0x58)
							{
								answerByte	=	boot_lock_fuse_bits_get(GET_HIGH_FUSE_BITS);
							}
							else
							{
								answerByte	=	0;
							}
						}
						else
						{
							answerByte	=	0; // for all others command are not implemented, return dummy value for AVRDUDE happy <Worapoht>
						}
						if ( !flag )
						{
							msgLength		=	7;
							msgBuffer[1]	=	STATUS_CMD_OK;
							msgBuffer[2]	=	0;
							msgBuffer[3]	=	msgBuffer[4];
							msgBuffer[4]	=	0;
							msgBuffer[5]	=	answerByte;
							msgBuffer[6]	=	STATUS_CMD_OK;
						}
					}
					break;
	#endif
				case CMD_SIGN_ON:
					msgLength		=	11;
					msgBuffer[1] 	=	STATUS_CMD_OK;
					msgBuffer[2] 	=	8;
					msgBuffer[3] 	=	'A';
					msgBuffer[4] 	=	'V';
					msgBuffer[5] 	=	'R';
					msgBuffer[6] 	=	'I';
					msgBuffer[7] 	=	'S';
					msgBuffer[8] 	=	'P';
					msgBuffer[9] 	=	'_';
					msgBuffer[10]	=	'2';
					break;

				case CMD_GET_PARAMETER:
					{
						unsigned char value;

						switch(msgBuffer[1])
						{
						case PARAM_BUILD_NUMBER_LOW:
							value	=	CONFIG_PARAM_BUILD_NUMBER_LOW;
							break;
						case PARAM_BUILD_NUMBER_HIGH:
							value	=	CONFIG_PARAM_BUILD_NUMBER_HIGH;
							break;
						case PARAM_HW_VER:
							value	=	CONFIG_PARAM_HW_VER;
							break;
						case PARAM_SW_MAJOR:
							value	=	CONFIG_PARAM_SW_MAJOR;
							break;
						case PARAM_SW_MINOR:
							value	=	CONFIG_PARAM_SW_MINOR;
							break;
						default:
							value	=	0;
							break;
						}
						msgLength		=	3;
						msgBuffer[1]	=	STATUS_CMD_OK;
						msgBuffer[2]	=	value;
					}
					break;

				case CMD_LEAVE_PROGMODE_ISP:
					isLeave	=	1;
					//*	fall thru

				case CMD_SET_PARAMETER:
				case CMD_ENTER_PROGMODE_ISP:
					msgLength		=	2;
					msgBuffer[1]	=	STATUS_CMD_OK;
					break;

				case CMD_READ_SIGNATURE_ISP:
					{
						unsigned char signatureIndex	=	msgBuffer[4];
						unsigned char signature;

						if ( signatureIndex == 0 )
							signature	=	(SIGNATURE_BYTES >>16) & 0x000000FF;
						else if ( signatureIndex == 1 )
							signature	=	(SIGNATURE_BYTES >> 8) & 0x000000FF;
						else
							signature	=	SIGNATURE_BYTES & 0x000000FF;

						msgLength		=	4;
						msgBuffer[1]	=	STATUS_CMD_OK;
						msgBuffer[2]	=	signature;
						msgBuffer[3]	=	STATUS_CMD_OK;
					}
					break;

				case CMD_READ_LOCK_ISP:
					msgLength		=	4;
					msgBuffer[1]	=	STATUS_CMD_OK;
					msgBuffer[2]	=	boot_lock_fuse_bits_get( GET_LOCK_BITS );
					msgBuffer[3]	=	STATUS_CMD_OK;
					break;

				case CMD_READ_FUSE_ISP:
					{
						unsigned char fuseBits;

						if ( msgBuffer[2] == 0x50 )
						{
							if ( msgBuffer[3] == 0x08 )
								fuseBits	=	boot_lock_fuse_bits_get( GET_EXTENDED_FUSE_BITS );
							else
								fuseBits	=	boot_lock_fuse_bits_get( GET_LOW_FUSE_BITS );
						}
						else
						{
							fuseBits	=	boot_lock_fuse_bits_get( GET_HIGH_FUSE_BITS );
						}
						msgLength		=	4;
						msgBuffer[1]	=	STATUS_CMD_OK;
						msgBuffer[2]	=	fuseBits;
						msgBuffer[3]	=	STATUS_CMD_OK;
					}
					break;

	#ifndef REMOVE_PROGRAM_LOCK_BIT_SUPPORT
				case CMD_PROGRAM_LOCK_ISP:
					{
						unsigned char lockBits	=	msgBuffer[4];

						lockBits	=	(~lockBits) & 0x3C;	// mask BLBxx bits
						boot_lock_bits_set(lockBits);		// and program it
						boot_spm_busy_wait();

						msgLength		=	3;
						msgBuffer[1]	=	STATUS_CMD_OK;
						msgBuffer[2]	=	STATUS_CMD_OK;
					}
					break;
	#endif
				case CMD_CHIP_ERASE_ISP:
					eraseAddress	=	0;
					msgLength		=	2;
				//	msgBuffer[1]	=	STATUS_CMD_OK;
					msgBuffer[1]	=	STATUS_CMD_FAILED;	//*	isue 543, return FAILED instead of OK
					break;

				case CMD_LOAD_ADDRESS:
	#if defined(RAMPZ)
					address	=	( ((address_t)(msgBuffer[1])<<24)|((address_t)(msgBuffer[2])<<16)|((address_t)(msgBuffer[3])<<8)|(msgBuffer[4]) )<<1;
	#else
					address	=	( ((msgBuffer[3])<<8)|(msgBuffer[4]) )<<1;		//convert word to byte address
	#endif
					msgLength		=	2;
					msgBuffer[1]	=	STATUS_CMD_OK;
					break;

				case CMD_SET_UPLOAD_SIZE_PRUSA3D:
					((unsigned char*)&flashSize)[0] = msgBuffer[1];
					((unsigned char*)&flashSize)[1] = msgBuffer[2];
					((unsigned char*)&flashSize)[2] = msgBuffer[3];
					((unsigned char*)&flashSize)[3] = 0;
					msgLength		=	2;
					msgBuffer[1]	=	STATUS_CMD_OK;
					break;

				case CMD_PROGRAM_FLASH_ISP:
				case CMD_PROGRAM_EEPROM_ISP:
					{
						unsigned int	size	=	((msgBuffer[1])<<8) | msgBuffer[2];
						unsigned char	*p	=	msgBuffer+10;
						unsigned int	data;
						unsigned char	highByte, lowByte;
						address_t		tempaddress	=	address;


						if ( msgBuffer[0] == CMD_PROGRAM_FLASH_ISP )
						{
							if (flashSize != 0)
							{
								if (address == 0) //first page
								{
									flashCounter = size; //initial value = size
									flashAddressLast = 0; //last 
									flashOperation = 1; //write
								}
								else if (address != flashAddressLast)
									flashCounter += size; //add size to counter
								flashAddressLast = address;
							}

							// erase only main section (bootloader protection)
							if (eraseAddress < APP_END ) //erase and write only blocks with address less 0x3e000
							{ //because prevent "brick"
									boot_page_erase(eraseAddress);	// Perform page erase
									boot_spm_busy_wait();		// Wait until the memory is erased.
									eraseAddress += SPM_PAGESIZE;	// point to next page to be erase
							}
							if (address < APP_END)
							{
								/* Write FLASH */
								do {
									lowByte		=	*p++;
									highByte 	=	*p++;

									data		=	(highByte << 8) | lowByte;
									boot_page_fill(address,data);

									address	=	address + 2;	// Select next word in memory
									size	-=	2;				// Reduce number of bytes to write by two
								} while (size);					// Loop until all bytes written

								boot_page_write(tempaddress);
								boot_spm_busy_wait();
								boot_rww_enable();				// Re-enable the RWW section
							}
						}
						else
						{
							//*	issue 543, this should work, It has not been tested.
							uint16_t ii = address >> 1;
							/* write EEPROM */
							while (size) {
								eeprom_write_byte((uint8_t*)ii, *p++);
								address+=2;						// Select next EEPROM byte
								ii++;
								size--;
							}
						}
						msgLength		=	2;
						msgBuffer[1]	=	STATUS_CMD_OK;

					}
					break;

				case CMD_READ_FLASH_ISP:
				case CMD_READ_EEPROM_ISP:
					{
						unsigned int	size	=	((msgBuffer[1])<<8) | msgBuffer[2];
						unsigned char	*p		=	msgBuffer+1;
						msgLength				=	size+3;

						*p++	=	STATUS_CMD_OK;
						if (msgBuffer[0] == CMD_READ_FLASH_ISP )
						{
							if (flashSize != 0)
							{
								if ((address == 0x00000) && (flashOperation == 1))
								{
									flashOperation = 2; //verify
									flashCounter = size; //initial value = size
								}
								else
									flashCounter += size; //add size to counter
							}

							unsigned int data;

							// Read FLASH
							do {
						//#if defined(RAMPZ)
						#if (FLASHEND > 0x10000)
								data	=	pgm_read_word_far(address);
						#else
								data	=	pgm_read_word_near(address);
						#endif
								*p++	=	(unsigned char)data;		//LSB
								*p++	=	(unsigned char)(data >> 8);	//MSB
								address	+=	2;							// Select next word in memory
								size	-=	2;
							}while (size);
						}
						else
						{
							/* Read EEPROM */
							do {
								EEARL	=	address;			// Setup EEPROM address
								EEARH	=	((address >> 8));
								address++;					// Select next EEPROM byte
								EECR	|=	(1<<EERE);			// Read EEPROM
								*p++	=	EEDR;				// Send EEPROM data
								size--;
							} while (size);
						}
						*p++	=	STATUS_CMD_OK;
					}
					break;

				default:
					msgLength		=	2;
					msgBuffer[1]	=	STATUS_CMD_FAILED;
					break;
			}

			/*
			 * Now send answer message back
			 */
			sendchar(MESSAGE_START);
			checksum	=	MESSAGE_START^0;

			sendchar(seqNum);
			checksum	^=	seqNum;

			c			=	((msgLength>>8)&0xFF);
			sendchar(c);
			checksum	^=	c;

			c			=	msgLength&0x00FF;
			sendchar(c);
			checksum ^= c;

			sendchar(TOKEN);
			checksum ^= TOKEN;

			p	=	msgBuffer;
			while ( msgLength )
			{
				c	=	*p++;
				sendchar(c);
				checksum ^=c;
				msgLength--;
			}
			sendchar(checksum);
			seqNum++;
	
		#ifndef REMOVE_BOOTLOADER_LED
			//*	<MLS>	toggle the LED
			PROGLED_PORT	^=	(1<<PROGLED_PIN);	// active high LED ON
		#endif

		}
	}




#ifndef REMOVE_BOOTLOADER_LED
	PROGLED_DDR		&=	~(1<<PROGLED_PIN);	// set to default
	PROGLED_PORT	&=	~(1<<PROGLED_PIN);	// active low LED OFF
//	PROGLED_PORT	|=	(1<<PROGLED_PIN);	// active high LED OFf
	delay_ms(100);							// delay after exit
#endif

exit:
	asm volatile ("nop");			// wait until port has changed

	/*
	 * Now leave bootloader
	 */

	UART_STATUS_REG	&=	0xfd;
	boot_rww_enable();				// enable application section


	asm volatile(
			"clr	r30		\n\t"
			"clr	r31		\n\t"
			"ijmp	\n\t"
			);
//	asm volatile ( "push r1" "\n\t"		// Jump to Reset vector in Application Section
//					"push r1" "\n\t"
//					"ret"	 "\n\t"
//					::);

	 /*
	 * Never return to stop GCC to generate exit return code
	 * Actually we will never reach this point, but the compiler doesn't
	 * understand this
	 */
	for(;;);
}

/*
base address = f800

avrdude: Device signature = 0x1e9703
avrdude: safemode: lfuse reads as FF
avrdude: safemode: hfuse reads as DA
avrdude: safemode: efuse reads as F5
avrdude>


base address = f000
avrdude: Device signature = 0x1e9703
avrdude: safemode: lfuse reads as FF
avrdude: safemode: hfuse reads as D8
avrdude: safemode: efuse reads as F5
avrdude>
*/

//************************************************************************
