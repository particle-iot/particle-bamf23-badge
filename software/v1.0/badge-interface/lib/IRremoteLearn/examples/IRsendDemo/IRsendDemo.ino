/*
 * IRremoteLearn: IRsendDemo - demonstrates sending IR codes with IRsend
 * An IR LED emitter and current limiting resistor must be connected to the input SEND_PIN
 *
 * Suggestion: Lumex OED-EL-1L2 https://www.digikey.com/products/en?keywords=OED-EL-1L2
 *             100 ohm resistor (3.3V - Vf(1.2)/25mA(GPIO Imax) = 84 ohms)
 */

#include "IRremoteLearn.h"

const int SEND_PIN = TX;

// defaults to TX pin if no pin specified, see IRsend::IRsend(int txpin) header for PWM pins available
IRsend irsend(SEND_PIN);

void setup() {
    pinMode(D7, OUTPUT);
}

void loop() {
    digitalWrite(D7, HIGH);
    irsend.sendSony(0x68B92, 20);
    delay(1000);

    irsend.sendNEC(0x1FE40BF, 32);
    delay(1000);

    irsend.sendPanasonic(0x4004, 0x100BCBD); // Address, Power toggle command
    delay(1000);

    irsend.sendJVC(0xC5E8, 16, 0); // Power command, hex value, 16 bits, no repeat
    delay(1000);

    irsend.sendRC5(0x123, 12);
    delay(1000);

    irsend.sendRC6(0x45678, 20);
    delay(1000);

    unsigned int buffer[10] = {400, 400, 1233, 800, 800};
    irsend.sendRaw(buffer, 5, 38);
    delay(1000);

    digitalWrite(D7, LOW);
    delay(3000);
}