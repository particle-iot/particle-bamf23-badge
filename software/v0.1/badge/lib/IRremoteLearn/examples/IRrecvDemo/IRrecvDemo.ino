/*
 * IRremoteLearn: IRrecvDemo - demonstrates receiving IR codes with IRrecv
 * An IR detector/demodulator must be connected to the input RECV_PIN.
 *
 * NOTE: This library expects a special non-demodulated Learner IR receiver
 * such as the Vishay TSMP58000 http://www.vishay.com/docs/82485/tsmp58000.pdf
 */


/*
 * EXAMPLE OUTPUT when paired with IRsendDemo
 *
 * Decoded SONY: 00068B92 (20 bits) Raw(41)
 * Decoded NEC: [VALUE MATCHED]: 01FE40BF (32 bits) Raw(67)
 * Decoded PANASONIC - Address: 4004 Value: 0100BCBD (48 bits) Raw(99)
 * Decoded JVC: 0000C5E8 (16 bits) Raw(35)
 * Unknown encoding: 80BC9608 (32 bits) Raw(21)  ... this is RC5, needs tuning
 * Unknown encoding: FFD81C30 (32 bits) Raw(33)  ... this is RC6, needs tuning
 */

#include "IRremoteLearn.h"

const int RECV_PIN = RX;

// Default 300ms idle timeout, 200us mark timeout
IRrecv irrecv(RECV_PIN);

// Optionally specify your own timeouts
// const unsigned long IDLE_TIMEOUT_MS = 500;
// const unsigned long MARK_TIMEOUT_US = 200;
// IRrecv irrecv(RECV_PIN, IDLE_TIMEOUT_MS, MARK_TIMEOUT_US);

decode_results results;

void setup()
{
    Serial.begin(9600);
    irrecv.enableIRIn(); // Start the receiver
}

void dump(decode_results *results) {
    // Dumps out the decode_results structure.
    // Call this after IRrecv::decode()
    int count = results->rawlen;
    if (results->decode_type == PROTOCOL_UNKNOWN) {
        Serial.print("Unknown encoding: ");
    }
    else if (results->decode_type == NEC) {
        Serial.print("Decoded NEC: ");

        // Test for a specific value, do some action!
        if (results->value == 0x1FE40BF) {
            Serial.print("[VALUE MATCHED]: ");
        }
    }
    else if (results->decode_type == SONY) {
        Serial.print("Decoded SONY: ");
    }
    else if (results->decode_type == RC5) {
        // TODO: Known issue RC5 decoding needs to be tuned!
        Serial.print("Decoded RC5: ");
    }
    else if (results->decode_type == RC6) {
        // TODO: Known issue RC6 decoding needs to be tuned!
        Serial.print("Decoded RC6: ");
    }
    else if (results->decode_type == PANASONIC) {
        Serial.print("Decoded PANASONIC - Address: ");
        Serial.print(results->address, HEX);
        Serial.print(" Value: ");
    }
    else if (results->decode_type == JVC) {
        Serial.print("Decoded JVC: ");
    }

    if (results->decode_type == DISNEY) {
        // TODO: Known issue DISNEY decoding needs to be tuned!
        // Need to add CRC check to ensure we have a valid DISNEY code.
        count = results->disney_len;
        Serial.print("Decoded DISNEY: ");
        for (int i=0; i<results->disney_len; i++) {
            Serial.printf("%02X",results->disney_data[i]);
        }
    } else {
        Serial.printf("%08X",results->value);
    }

    Serial.printlnf(" (%d bits) Raw(%d)", results->bits, count);

    // Optional deep dive into data (high,-low,high,-low)
    // for (int i = 0; i < count; i++) {
    //     Serial.printf("%s%lu,", (i%2)?"-":"",results->rawbuf[i]);
    // }
    // Serial.println();

}

void loop() {
  if (irrecv.decode(&results)) {
    dump(&results);
    irrecv.resume(); // Receive the next value
  }
}
