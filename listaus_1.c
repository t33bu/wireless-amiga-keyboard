/* Amiga wireless keyboard transmitter                         *
 * The  missing code example from the Skrolli 2018.3 article   *
 * Teemu Leppänen (tjlepp@gmail.com)                           */

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
	while (TRUE) {
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
				case PS2_RELEASE:   // Set flags
					ps2release = TRUE;
					resetstate = FALSE;
					break;
				case PS2_EXTENDED: // Set flag
					ps2keyext = TRUE;
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
						sendamiga((lookup(ps2key,ps2keyext) << 1) + amigacaps);
						amigacaps = !amigacaps;
						sendledcmd((amigacaps * PS2CAPSLOCK_LED) + (ledstate & PS2OTHER_LED));
					}
					clearflags();
					break;
			default: // All other keys
                // Handle reset logic
				if (ps2key == PS2_SYSRQ) { 
					resetstate = PS2RESET_ALL;
				} else if (!ps2release && (ps2key == PS2_LCTRL)) {
					resetstate += PS2RESET_CTRL;
				} else if (!ps2release && (ps2key == PS2_LALT)) {
					resetstate += PS2RESET_ALT;
				} else if (!ps2release && (ps2key == PS2_DEL)) {
					resetstate += PS2RESET_DEL;
				}
				if (resetstate == PS2RESET_ALL) {
					amigakey = AMIGA_RESET;
					resetstate = FALSE;
                // Normal key
				} else {
					amigakey = lookup(ps2key,ps2keyext);
				}
				// Send amiga keycode				
                sendamiga((amigakey << 1) + ps2release);
				clearflags();
				break;
			} // switch
			clearbuffers();
		} // if
	} // while
}

void sendledcmd(uint8_t byte) {
	ledstate = byte;
	ledcmd = PS2_SETLEDS; // State variable
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
