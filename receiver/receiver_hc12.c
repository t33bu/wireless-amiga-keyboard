/* receiver_hc12.c                                                                    *
 * by Teemu Leppänen (tjlepp@gmail.com)                                               *
 * This work is licensed under Creative Commons Attribution-NonCommercial-ShareAlike  *
 * CC BY-NC-SA 4.0, https://creativecommons.org/licenses/by-nc-sa/4.0/)               */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>

#define	_KBCLOCK			PB7
#define	_KBDATA				PB6
#define	_KBRESET			PB5
#define	_KBSTATUS			PB1
#define	_KBINUSE			PB0
#define	AMIGA_RESET			0x78
#define AMIGA_INITPOWER		0xFD
#define AMIGA_TERMPOWER		0xFE

void kb_startup(void);
void kb_send(uint8_t code);
void kb_reset(void);

enum PROGRAM_STATE { IDLE, SYNC, RECEIVED };
volatile uint8_t mode=IDLE, uartdata=0, uartstate=IDLE, ledstate=0;
uint8_t leds=0;

ISR(USART_RX_vect){

	uartdata = UDR;
	mode = RECEIVED;
}

ISR(TIMER1_COMPA_vect) {

	if (mode == IDLE) {

		// sync-signaali
		PORTB &= ~(1 << _KBDATA);
		mode = SYNC;
	}
}

// **************************
ISR(PCINT_vect) {

	leds = PINB & 0x03;	// PB0 ja PB1
}

// **************************
int main() {

	cli();

	// Led pins as inputs
	DDRB &= ~(1 << _KBSTATUS);
	DDRB &= ~(1 << _KBINUSE);
	PORTB &= ~(1 << _KBSTATUS);
	PORTB &= ~(1 << _KBINUSE);
	// Change in led pins raises interrupts
	PCMSK |= (1 << PCINT1) | (1 << PCINT0);
	GIMSK |= (1 << PCIE);

	// MCU controls data and clock pins
	DDRB  |= (1 << _KBCLOCK) | (1 << _KBDATA) | (1 << _KBRESET);
	PORTB |= (1 << _KBCLOCK) | (1 << _KBDATA) | (1 << _KBRESET);

	// Serial commm
	UBRRH = 0;
	UBRRL = 103; // 16Mhz, 9600bps
	UCSRB = (1 << RXCIE) | (1 << RXEN) | (1 << TXEN);
	UCSRC = (1 << UCSZ1) | (1 << UCSZ0);

	// Timer for synchronization
	TCCR1B |= (1 << WGM12);
	TIMSK |= (1 << OCIE1A);
	OCR1AH = 0x3D; // Once per second
	OCR1AL = 0x09;
	TCCR1B |= (1<<CS12) | (1<<CS10);

	sei();

	kb_startup();

	while (1) {

		// lopeta sync, kun > 1us
		if ((mode == SYNC) && (TCNT1 > 0)) {
			PORTB |= (1 << _KBDATA);
			mode = IDLE;
		}

		if (uartdata > 0) {
			if (uartdata == (AMIGA_RESET << 1)) {
				kb_reset();
			} else  {
				kb_send(uartdata);
			}
			uartdata = 0;
			uartstate = IDLE;
		}

		leds = PINB >> 6; 		     // Check led pin state
		if (leds != ledstate) {
			while (!(UCSRA & (1 << UDRE)));
			UDR = leds;
			ledstate = leds;
		}
	}

	return 0;
}

// **************************
void kb_startup() {

	_delay_ms(1000); // wait for sync (handled by TIMER1)
	kb_send(AMIGA_INITPOWER << 1); // send "initiate power-up"
	_delay_us(200);
	kb_send(AMIGA_TERMPOWER << 1); 	// send "terminate power-up"
}

// **************************
void kb_send(uint8_t code) {

	uint8_t i=0, bit = 0x80;
	for (i=0; i < 8; i++) {

		if (code & bit) {
			PORTB &= ~(1 << _KBDATA);
		} else {
			PORTB |= (1 << _KBDATA);
		}
		bit >>= 1;
		_delay_us(20);	// kb dat line low for 20us before clk
		PORTB &= ~(1 << _KBCLOCK);
		_delay_us(20);	// clk low for ~20us
		PORTB |= (1 << _KBCLOCK);
		_delay_us(50);  // clk high for ~55us
	}
	PORTB |= (1 << _KBDATA);
	DDRB &= ~(1 << _KBDATA); // dat to input
	_delay_ms(5);	// handshake wait  up to 143ms
	DDRB |= (1 << _KBDATA); // dat to output
}

// **************************
void kb_reset(void) {

	_delay_ms(500); 	// wait for reset warning
	PORTB &= ~(1 << _KBRESET); 	// reset
	_delay_ms(500); 	// minimum 250ms
	PORTB |= (1 << _KBRESET);
}
