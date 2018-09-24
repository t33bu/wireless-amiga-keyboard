/* Amiga wireless keyboard receiver                            *
 * The  missing code example from the Skrolli 2018.3 article   *
 * Teemu Leppänen (tjlepp@gmail.com)                           */

ISR(USART_RX_vect){
	uartdata = UDR;
	uartstate = RECEIVED;
}

ISR(TIMER1_COMPA_vect) {
	PORTB &= ~(1 << _KBDATA);     // Set sync signal
	syncstate = SYNC;
}

int main() {
	init_board();
	kb_startup();
	while (1) {
		// Reset sync signal, when > 1us
		if ((syncstate == SYNC) && (TCNT1 > 0)) {
			PORTB |= (1 << _KBDATA);
			syncstate = IDLE;
		}
		if (uartstate == RECEIVED) { // Keycode to Amiga
			if (uartdata == (AMIGA_RESET << 1)) {
				kb_reset();          // Keycode is reset
			} else  {
				kb_send(uartdata);
			}
			uartdata = 0;
			uartstate = IDLE;
		}
		leds = PINB & LEDPINIDS;    // Check led pin state
		if (leds != ledstate) {
			while (!(UCSRA & (1 << UDRE)));
			UDR = leds;
			ledstate = leds;
		}
	}
	return 0;
}

void kb_startup() {
	_delay_ms(1000); 				// Just wait for sync
	kb_send(AMIGA_INITPOWER << 1);	// Initiate power-up
	_delay_us(200);                 
	kb_send(AMIGA_TERMPOWER << 1);	// Terminate power-up
}

void kb_send(uint8_t code) {
	uint8_t i=0, databit = 0x80;
	for (i=0; i < 8; i++) {			// Set/reset data line
		if (code & databit) {
			PORTB &= ~(1 << _KBDATA);
		} else {
			PORTB |= (1 << _KBDATA);
		}
		databit >>= 1;              // Next bit
		_delay_us(20);
		PORTB &= ~(1 << _KBCLOCK);  // Clock low
		_delay_us(20);
		PORTB |= (1 << _KBCLOCK);	// Clock high
		_delay_us(50);
	}
	PORTB |= (1 << _KBDATA);		// Handshake with Amiga
	DDRB &= ~(1 << _KBDATA); 		// Release data line
	_delay_ms(5);					// Max delay 143ms
	DDRB |= (1 << _KBDATA); 		// Hold data line
}

void kb_reset(void) {
	PORTB &= ~(1 << _KBRESET); 		// Reset line low > 250ms
	_delay_ms(500);
	PORTB |= (1 << _KBRESET);		// Reset line high
}
