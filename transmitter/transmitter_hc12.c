/* transmitter_hc12.c                                                                 *
 * by Teemu Leppänen (tjlepp@gmail.com)                                               *
 * This work is licensed under Creative Commons Attribution-NonCommercial-ShareAlike  *
 * CC BY-NC-SA 4.0, https://creativecommons.org/licenses/by-nc-sa/4.0/)               */
 
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <util/delay.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "scancodes.h"

#define	CLK 				PD3
#define	DATA 				PD4

void initboard(void);
void sendamiga(uint8_t keycode);
void sendkeyboard(uint8_t byte);
void sendledcmd(uint8_t byte);
void clearflags();
void clearbuffers(void);
uint8_t lookupstandard(uint8_t lkey);
uint8_t lookupextended(uint8_t lkey);
uint8_t oddparity(uint8_t data);
uint8_t reversebyte(uint8_t b);

enum PROGRAM_STATE { IDLE, KEYREADY, RECEIVED };
volatile uint16_t ps2keycode=0;
volatile uint8_t ps2bitcnt=0, uartdata=0, ledstate=0, ps2state = IDLE, uartstate = IDLE, ledcmd = IDLE;
uint8_t resetstate = 0, amigakey=0, amigacaps=0, ps2key=0, ps2keyext=0, ps2release=0;

ISR(INT1_vect) {
	ps2keycode <<= 1;
	if (PIND & (1 << DATA)) {
		ps2keycode++;
	}
	if (++ps2bitcnt == PS2_BITCOUNT) {
		ps2state = KEYREADY;
	}
}

ISR(USART_RX_vect){
	uartdata = UDR;
	uartstate = RECEIVED;
}

int main() {
	initboard();
	while (true) {
		if (uartstate == RECEIVED) { 	// Led cmd received
			sendledcmd((ledstate & PS2CAPSLOCK_LED) | uartdata);
			uartstate = IDLE;
		}
		if (ps2state == KEYREADY) {	// Translate keycode
			ps2key = reversebyte(ps2keycode >> 2);
			switch (ps2key) {
				case PS2_RESEND: 	// Ignored keycodes
				case PS2_ERROR:
				case PS2_SELFTEST:
					break;
				case PS2_RELEASE:
					ps2release = true;
					resetstate = false;
					break;
				case PS2_EXTENDED:
					ps2keyext = true;
					break;
				case PS2_ACK: // Cmd ack received
					if (ledcmd == PS2_SETLEDS) {
						sendkeyboard(ledstate);
						ledcmd = IDLE;
						clearbuffers();
					}
					break;
				case PS2_CAPSLOCK: // Handle capsLock state
					if (!ps2release) { // Ignore release key
						sendamiga((lookupstandard(ps2key) << 1) + amigacaps);
						amigacaps = !amigacaps;
						sendledcmd((amigacaps * PS2CAPSLOCK_LED) + (ledstate & PS2OTHER_LED));
					}
					clearflags();
					break;
			default: // All other keys
				if (ps2key == PS2_SYSRQ) { // Reset keys
					resetstate = PS2RESET_ALL;
				} else if (!ps2release && (ps2key == PS2_LCTRL)) {
					resetstate += PS2RESET_CTRL;
				} else if (!ps2release && (ps2key == PS2_LALT)) {
					resetstate += PS2RESET_ALT;
				} else if (!ps2release && (ps2key == PS2_DEL)) {
					resetstate += PS2RESET_DEL;
				}
				// Send reset
				if (resetstate == PS2RESET_ALL) {
					amigakey = AMIGA_RESET;
					resetstate = false;
				// Send amiga keycode
				} else if (ps2keyext) {
					amigakey = lookupextended(ps2key);
				} else {
					amigakey = lookupstandard(ps2key);
				}
				sendamiga((amigakey << 1) + ps2release);
				clearflags();
				break;
			} // switch
			clearbuffers();
		} // if
	} // while
}

void initboard() {
	cli();
	DDRD &= ~(1 << CLK);	// Setup keyboard pins
	DDRD &= ~(1 << DATA);
	PORTD |= (1 << CLK) | (1 << DATA);
	UBRRH = 0;				// Serial comm 16Mhz, 9600bps
	UBRRL = 103;
	UCSRB = (1 << RXCIE) | (1 << RXEN) | (1 << TXEN);
	UCSRC = (1 << UCSZ1) | (1 << UCSZ0);
	// Keyboard interrupt INT1 (PD3) falling edge
	MCUCR |= (1 << ISC11);
	GIMSK |= (1 << INT1);
	sei();
}

void sendledcmd(uint8_t byte) {
	ledstate = byte;
	ledcmd = PS2_SETLEDS;
	sendkeyboard(ledcmd);
	clearbuffers();
}

void sendamiga(uint8_t keycode) {
	while (!(UCSRA & (1 << UDRE)));
	UDR = keycode;
}

void sendkeyboard(uint8_t databyte) {
	uint16_t databit = PS2_BITMASK;
	uint16_t data = reversebyte(databyte);
	data <<= 2;
	data += (oddparity(databyte) << 1) + 1; // Last 1 stop bit
	// Sequence:	1. Clock low > 100us
	// 				2. Data Low
	// 				3. Release clock
	DDRD |= (1 << CLK);
	PORTD &= ~(1 << CLK);
	_delay_us(200);
	DDRD |= (1 << DATA);
	PORTD &= ~(1 << DATA);
	_delay_us(50); // ..time to react
	PORTD |= (1 << CLK);
	DDRD &= ~(1 << CLK);
	// 				4. Wait for clock low
	while (PIND & (1 << CLK)) ;
	while (databit > 0) {
		// 			5. Set/reset data line
		if (data & databit)
			PORTD |= (1 << DATA);
		else
			PORTD &= ~(1 << DATA);
		databit >>= 1;
		// 			6. Wait for clock high
		while ((PIND & (1 << CLK)) == 0) ;
		// 			7. Wait for clock low
	    while (PIND & (1 << CLK)) ;
	} // 			8. Repeat for all bits
	// 				9. Release data
	DDRD &= ~(1 << DATA);
	// 				10. Wait data low
	while (PIND & (1 << DATA)) ;
	// 				11. Wait clock low
	while (PIND & (1 << CLK)) ;
}

void clearflags() {
	ps2keyext = false;
	ps2release = false;
}

void clearbuffers() {
	ps2bitcnt=0;
	ps2keycode=0;
	ps2state = IDLE;
}

uint8_t lookupstandard(uint8_t lkey) {
	uint8_t i=0,keyvalue=lkey;
	for (i=0; i < PS2_SCANCODE_SIZE; i++)
		if (lkey == pgm_read_byte(&ps2standard[i][0])) {
			keyvalue = pgm_read_byte(&ps2standard[i][1]);
			break;
		}
	return keyvalue;
}

uint8_t lookupextended(uint8_t lkey) {
	uint8_t i=0,keyvalue=lkey;
	for (i=0; i < PS2_SCANCODE_EXT_SIZE; i++)
		if (lkey == pgm_read_byte(&ps2extended[i][0])) {
			keyvalue = pgm_read_byte(&ps2extended[i][1]);
			break;
		}
	return keyvalue;
}

uint8_t oddparity(uint8_t data) {
	uint8_t parity=0, databit=0x80;
	while (databit > 0) {
		if (data & databit)
			parity++;
		databit >>= 1;
	}
	return !(parity % 2);
}

uint8_t reversebyte(uint8_t b) {
	b = ((b >> 1) & 0x55) | ((b << 1) & 0xaa);
    b = ((b >> 2) & 0x33) | ((b << 2) & 0xcc);
    b = ((b >> 4) & 0x0f) | ((b << 4) & 0xf0);
    return b;
}
