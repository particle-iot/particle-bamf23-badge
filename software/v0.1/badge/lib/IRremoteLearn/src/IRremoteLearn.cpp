/*
 * IRremoteLearn
 *
 * This library expects a special non-demodulated Learner IR receiver
 * such as the Vishay TSMP58000 http://www.vishay.com/docs/82485/tsmp58000.pdf
 * and uses an interrupt handler to strip the carrier from the IR signal.
 * Although it should also work with normal demodulated receivers.
 *
 * TODO: Add the ability to report what the carrier frequency is.
 *
 * Author: Brett Walach (ported to Particle on July 22nd 2018)
 *
 * ---------------
 * IRremote
 * Version 0.11 August, 2009
 * Copyright 2009 Ken Shirriff
 * For details, see http://arcfn.com/2009/08/multi-protocol-infrared-remote-library.html
 *
 * Modified by Paul Stoffregen <paul@pjrc.com> to support other boards and timers
 * Modified  by Mitra Ardron <mitra@mitra.biz>
 * Added Sanyo and Mitsubishi controllers
 * Modified Sony to spot the repeat codes that some Sony's send
 *
 * Interrupt code based on NECIRrcv by Joe Knapp
 * http://www.arduino.cc/cgi-bin/yabb2/YaBB.pl?num=1210243556
 * Also influenced by http://zovirl.com/2008/11/12/building-a-universal-remote-with-an-arduino/
 *
 * JVC and Panasonic protocol added by Kristian Lauszus (Thanks to zenwheel and other people at the original blog post)
 *
 * Added Interrupt driven timing
 */

#include "Particle.h"

#include "IRremoteLearn.h"
#include "IRremoteLearnInt.h"

volatile irparams_t irparams;

// These versions of MATCH, MATCH_MARK, and MATCH_SPACE are only for debugging.
// To use them, define DEBUG_IR in IRremoteLearnInt.h
// Normally macros are used for efficiency
#ifdef DEBUG_IR
int MATCH(int measured, int desired) {
    Serial.print("Testing: ");
    Serial.print(TICKS_LOW(desired), DEC);
    Serial.print(" <= ");
    Serial.print(measured, DEC);
    Serial.print(" <= ");
    Serial.print(TICKS_HIGH(desired), DEC);
    bool rv = measured >= TICKS_LOW(desired) && measured <= TICKS_HIGH(desired);
    Serial.printlnf(" = %s", (rv) ? "pass" : "fail");
    return rv;
}

int MATCH_MARK(int measured_ticks, int desired_us) {
    Serial.print("Testing mark ");
    Serial.print(measured_ticks * USECPERTICK, DEC);
    Serial.print(" vs ");
    Serial.print(desired_us, DEC);
    Serial.print(": ");
    Serial.print(TICKS_LOW(desired_us + MARK_EXCESS), DEC);
    Serial.print(" <= ");
    Serial.print(measured_ticks, DEC);
    Serial.print(" <= ");
    Serial.print(TICKS_HIGH(desired_us + MARK_EXCESS), DEC);
    bool rv = measured_ticks >= TICKS_LOW(desired_us + MARK_EXCESS) && measured_ticks <= TICKS_HIGH(desired_us + MARK_EXCESS);
    Serial.printlnf(" = %s", (rv) ? "pass" : "fail");
    return rv;

}

int MATCH_SPACE(int measured_ticks, int desired_us) {
    Serial.print("Testing space ");
    Serial.print(measured_ticks * USECPERTICK, DEC);
    Serial.print(" vs ");
    Serial.print(desired_us, DEC);
    Serial.print(": ");
    Serial.print(TICKS_LOW(desired_us - MARK_EXCESS), DEC);
    Serial.print(" <= ");
    Serial.print(measured_ticks, DEC);
    Serial.print(" <= ");
    Serial.print(TICKS_HIGH(desired_us - MARK_EXCESS), DEC);
    bool rv = measured_ticks >= TICKS_LOW(desired_us - MARK_EXCESS) && measured_ticks <= TICKS_HIGH(desired_us - MARK_EXCESS);
    Serial.printlnf(" = %s", (rv) ? "pass" : "fail");
    return rv;
}
#endif

void memset_volatile(volatile void *s, uint8_t c, size_t n)
{
    volatile uint8_t* p = (uint8_t*)s;
    while (n-- > 0) {
        *p++ = c;
    }
}

void memcpy_volatile(volatile void *d, volatile void *s, size_t n)
{
    // Serial.printf("Size: %lu", n);
    volatile uint32_t* src = (uint32_t*)s;
    volatile uint32_t* dest = (uint32_t*)d;
    while (n-- > 0) {
        *dest++ = *src++;
    }
}

uint8_t crc8(uint8_t data[], uint8_t len)
{
    uint8_t cs = 0;

    for ( int i = 0; i < len; i++ ) {
        cs ^= data[i];
    }

    return cs;
}

// prototype ISR handler
void ir_recv_handler();

void idle_timeout() {
    irparams.rcvstate = STATE_STOP;
    ir_recv_handler();
}

Timer idle_timer(irparams.mark_timout_us, idle_timeout, true); // one shot timer

/**
 * TIMING DIAGRAM
 *
 * HIGH
 * ~¯¯¯¯¯¯¯\__/¯¯\__/¯¯\__/¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯\__/¯¯\__/¯¯\__/¯¯¯¯(>irparams.idle_timout_us)¯¯¯¯¯~
 * LOW     |              |                |              |
 * IDLE----|-----MARK-----|------SPACE-----|-----MARK-----|------IDLE--------------------------
 *
 */
void ir_recv_handler() {
    // prevent re-entry
    static bool active = false;
    if (active) {
        return;
    }
    active = true;

    // irparams.current_time = System.ticks()/120; // optional hardware timer on Photon/P1/Electron
    irparams.current_time = micros();
    uint8_t irdata = (uint8_t)pinReadFast(irparams.rxpin);

    // Check for buffer overflow
    if (irparams.rawlen >= RAWBUF) {
        irparams.rcvstate = STATE_STOP;
    }

    switch (irparams.rcvstate) {
    case STATE_IDLE:
        if (irdata == MARK) {
            irparams.rawlen = 0;
            irparams.start_time = irparams.current_time;
            irparams.end_time = irparams.current_time;
            irparams.rcvstate = STATE_MARK;
            idle_timer.startFromISR();
        }
        break;
    case STATE_MARK: // SPACE handled here as well
        if (irdata == SPACE) {
            irparams.end_time = irparams.current_time;
        } else {
            if ((irparams.current_time - irparams.end_time) >= (irparams.mark_timout_us) ) {
                // save MARK
                irparams.rawbuf1[irparams.rawlen++] = (irparams.end_time - irparams.start_time);
                // Serial.printf("%lu,", irparams.rawbuf1[irparams.rawlen-1]);
                // pick out the SPACE timing
                irparams.start_time = irparams.end_time; // MARK end is SPACE start
                irparams.end_time = irparams.current_time; // current time is SPACE end
                // save SPACE
                irparams.rawbuf1[irparams.rawlen++] = (irparams.end_time - irparams.start_time);
                // Serial.printf("%lu,", irparams.rawbuf1[irparams.rawlen-1]);
                // reset timers
                irparams.start_time = irparams.current_time;
                irparams.end_time = irparams.current_time;
            }
        }
        break;
    case STATE_STOP:
        detachInterrupt(irparams.rxpin);
        idle_timer.stopFromISR();
        irparams.rawbuf1[irparams.rawlen++] = (irparams.end_time - irparams.start_time); // save last mark
        // Serial.printlnf("%lu", irparams.rawbuf1[irparams.rawlen-1]);
        memset_volatile(irparams.rawbuf2, 0, RAWBUF); // double buffer so we can decode while saving another capture
        memcpy_volatile(&irparams.rawbuf2, &irparams.rawbuf1, irparams.rawlen);
        memset_volatile(irparams.rawbuf1, 0, RAWBUF);
        irparams.start_time = irparams.current_time;
        irparams.end_time = irparams.current_time;
        irparams.rcvstate = STATE_CAPTURED;
        break;
    }

    if (irparams.blinkflag) {
        if (irdata == MARK) {
            BLINKLED_ON();  // turn pin D7 LED on
        }
        else {
            BLINKLED_OFF(); // turn pin D7 LED off
        }
    }

    active = false;
}

void IRsend::sendNEC(unsigned long data, int nbits)
{
    enableIROut(38);
    delayMicroseconds(2000);
    mark(NEC_HDR_MARK);
    space(NEC_HDR_SPACE);
    for (int i = 0; i < nbits; i++) {
        if (data & TOPBIT) {
            mark(NEC_BIT_MARK);
            space(NEC_ONE_SPACE);
        }
        else {
            mark(NEC_BIT_MARK);
            space(NEC_ZERO_SPACE);
        }
        data <<= 1;
    }
    mark(NEC_BIT_MARK);
    space(0);
}

/* Send a number of bytes, with additional CRC, based on NEC timing */
/* add LENGTH byte first, and CRC byte last */
void IRsend::sendBytes(uint8_t data[], int len)
{
    enableIROut(38);
    delayMicroseconds(2000);
    mark(NEC_HDR_MARK);
    space(NEC_HDR_SPACE);

    uint8_t crc_byte = crc8(data, len);
    uint8_t length_byte = len + 2;

    // Send LENGTH byte
    for (int i = 0; i < 8; i++) {
        if (length_byte & 0x80) {
            mark(NEC_BIT_MARK);
            space(NEC_ONE_SPACE);
        }
        else {
            mark(NEC_BIT_MARK);
            space(NEC_ZERO_SPACE);
        }
        length_byte <<= 1;
    }

    // space(NEC_ONE_SPACE * 4);

    // Send bytes
    for (int x = 0; x < len; x++) {
        uint8_t byte_to_send = data[x];
        for (int y = 0; y < 8; y++) {
            if (byte_to_send & 0x80) {
                mark(NEC_BIT_MARK);
                space(NEC_ONE_SPACE);
            }
            else {
                mark(NEC_BIT_MARK);
                space(NEC_ZERO_SPACE);
            }
            byte_to_send <<= 1;
        }

        // space(NEC_ONE_SPACE * 4);
    }

    // Send CRC byte
    for (int i = 0; i < 8; i++) {
        if (crc_byte & 0x80) {
            mark(NEC_BIT_MARK);
            space(NEC_ONE_SPACE);
        }
        else {
            mark(NEC_BIT_MARK);
            space(NEC_ZERO_SPACE);
        }
        crc_byte <<= 1;
    }

    // space(NEC_ONE_SPACE * 4);

    mark(NEC_BIT_MARK);
    space(0);
}

void IRsend::sendSony(unsigned long data, int nbits) {
    enableIROut(38);
    mark(SONY_HDR_MARK);
    space(SONY_HDR_SPACE);
    data = data << (32 - nbits);
    for (int i = 0; i < nbits; i++) {
        if (data & TOPBIT) {
            mark(SONY_ONE_MARK);
            space(SONY_HDR_SPACE);
        }
        else {
            mark(SONY_ZERO_MARK);
            space(SONY_HDR_SPACE);
        }
        data <<= 1;
    }
}

void IRsend::sendRaw(unsigned int buf[], int len, int hz)
{
    enableIROut(hz);
    for (int i = 0; i < len; i++) {
        if (i & 1) {
            space(buf[i]);
        }
        else {
            mark(buf[i]);
        }
    }
    space(0); // Just to be sure
}

// Note: first bit must be a one (start bit)
void IRsend::sendRC5(unsigned long data, int nbits)
{
    enableIROut(38);
    data = data << (32 - nbits);
    mark(RC5_T1); // First start bit
    space(RC5_T1); // Second start bit
    mark(RC5_T1); // Second start bit
    for (int i = 0; i < nbits; i++) {
        if (data & TOPBIT) {
            space(RC5_T1); // 1 is space, then mark
            mark(RC5_T1);
        }
        else {
            mark(RC5_T1);
            space(RC5_T1);
        }
        data <<= 1;
    }
    space(0); // Turn off at end
}

// Caller needs to take care of flipping the toggle bit
void IRsend::sendRC6(unsigned long data, int nbits)
{
    enableIROut(38);
    data = data << (32 - nbits);
    mark(RC6_HDR_MARK);
    space(RC6_HDR_SPACE);
    mark(RC6_T1); // start bit
    space(RC6_T1);
    int t;
    for (int i = 0; i < nbits; i++) {
        if (i == 3) {
            // double-wide trailer bit
            t = 2 * RC6_T1;
        }
        else {
            t = RC6_T1;
        }
        if (data & TOPBIT) {
            mark(t);
            space(t);
        }
        else {
            space(t);
            mark(t);
        }

        data <<= 1;
    }
    space(0); // Turn off at end
}
void IRsend::sendPanasonic(unsigned int address, unsigned long data) {
    enableIROut(38);
    mark(PANASONIC_HDR_MARK);
    space(PANASONIC_HDR_SPACE);

    for (int i = 0; i < 16; i++)
    {
        mark(PANASONIC_BIT_MARK);
        if (address & 0x8000) {
            space(PANASONIC_ONE_SPACE);
        } else {
            space(PANASONIC_ZERO_SPACE);
        }
        address <<= 1;
    }
    for (int i = 0; i < 32; i++) {
        mark(PANASONIC_BIT_MARK);
        if (data & TOPBIT) {
            space(PANASONIC_ONE_SPACE);
        } else {
            space(PANASONIC_ZERO_SPACE);
        }
        data <<= 1;
    }
    mark(PANASONIC_BIT_MARK);
    space(0);
}
void IRsend::sendJVC(unsigned long data, int nbits, int repeat)
{
    enableIROut(38);
    data = data << (32 - nbits);
    if (!repeat) {
        mark(JVC_HDR_MARK);
        space(JVC_HDR_SPACE);
    }
    for (int i = 0; i < nbits; i++) {
        if (data & TOPBIT) {
            mark(JVC_BIT_MARK);
            space(JVC_ONE_SPACE);
        }
        else {
            mark(JVC_BIT_MARK);
            space(JVC_ZERO_SPACE);
        }
        data <<= 1;
    }
    mark(JVC_BIT_MARK);
    space(0);
}

void IRsend::mark(int time) {
    // Sends an IR mark for the specified number of microseconds.
    // The mark output is modulated at the PWM frequency.
    analogWrite(irparams.txpin, 128, irparams.irout_khz * 1000); // Enable PWM output
    if (time > 0) delayMicroseconds(time);
}

/* Leave pin off for time (given in microseconds) */
void IRsend::space(int time) {
    // Sends an IR space for the specified number of microseconds.
    // A space is no output, so the PWM output is disabled.
    analogWrite(irparams.txpin, 0, irparams.irout_khz * 1000); // Disable PWM output
    if (time > 0) delayMicroseconds(time);
}

void IRsend::enableIROut(int khz) {
    // Enables IR output.  The khz value controls the modulation frequency in kilohertz.
    // The IR output will be on irparams.txpin
    irparams.irout_khz = khz;

    pinMode(irparams.txpin, OUTPUT);
    analogWrite(irparams.txpin, 0, irparams.irout_khz * 1000); // Disable PWM output
}

/*
PWM PINs for Particle
---
Spark Core: D0, D1, A0, A1, A4, A5, A6, A7, RX and TX.
Photon, P1 and Electron: D0, D1, D2, D3, A4, A5, WKP, RX and TX with a caveat:
  PWM timer peripheral is duplicated on two pins (A5/D2) and (A4/D3) for 7 total
  independent PWM outputs. For example: PWM may be used on A5 while D2 is used as a GPIO,
  or D2 as a PWM while A5 is used as an analog input. However A5 and D2 cannot be used
  as independently controlled PWM outputs at the same time.
Additionally on the Electron: B0, B1, B2, B3, C4, C5.
Additionally on the P1: P1S0, P1S1, P1S6 (note: for P1S6, the WiFi Powersave Clock should be disabled for complete control of this pin. See System Features).
*/
IRsend::IRsend(int txpin)
{
    irparams.txpin = txpin;
}

IRrecv::IRrecv(int rxpin, unsigned long idle_timout_ms, unsigned long mark_timout_us)
{
    irparams.rxpin = rxpin;
    irparams.idle_timout_ms = idle_timout_ms;
    irparams.mark_timout_us = mark_timout_us;
    irparams.blinkflag = 0;
}

// initialization
void IRrecv::enableIRIn() {
    // set pin modes
    pinMode(irparams.rxpin, INPUT);

    // enable and reset ir_recv_handler for Learner style IR receivers such as the Vishay TSMP58000
    // http://www.vishay.com/docs/82485/tsmp58000.pdf
    resume();
}

// initialization
void IRrecv::disableIRIn() {
    detachInterrupt(irparams.rxpin);
    idle_timer.stop();
}

// enable/disable blinking of pin 13 on IR processing
void IRrecv::blink13(int blinkflag)
{
    irparams.blinkflag = blinkflag;
    if (blinkflag) {
        pinMode(BLINKLED, OUTPUT);
    }
}


void IRrecv::resume() {
    irparams.rcvstate = STATE_IDLE;
    irparams.rawlen = 0;
    attachInterrupt(irparams.rxpin, ir_recv_handler, CHANGE);
}

// Decodes the received IR message
// Returns 0 if no data ready, 1 if data ready.
// Results of decoding are stored in results
int IRrecv::decode(decode_results *results) {
    results->rawbuf = irparams.rawbuf2;
    results->rawlen = irparams.rawlen;
    if (irparams.rcvstate != STATE_CAPTURED) {
        return ERR;
    }

    // For debugging when there is no match
    // for (int i = 0; i < results->rawlen; i++) {
    //   Serial.printf("%s%lu,", (i%2)?"-":"",results->rawbuf[i]);
    // }
    // Serial.println();

// #ifdef DEBUG_IR
//     Serial.println("Attempting NEC decode");
// #endif
//     if (decodeNEC(results)) {
//         return DECODED;
//     }

#ifdef DEBUG_IR
    Serial.println("Attempting BYTES decode");
#endif
    if (decodeBytes(results)) {
        return DECODED;
    }

// #ifdef DEBUG_IR
//   Serial.println("Attempting Panasonic decode");
// #endif
//   if (decodePanasonic(results)) {
//     return DECODED;
//   }

// #ifdef DEBUG_IR
//   Serial.println("Attempting Sony decode");
// #endif
//   if (decodeSony(results)) {
//     return DECODED;
//   }

// #ifdef DEBUG_IR
//   Serial.println("Attempting Sanyo decode");
// #endif
//   if (decodeSanyo(results)) {
//     return DECODED;
//   }

// #ifdef DEBUG_IR
//   Serial.println("Attempting Mitsubishi decode");
// #endif
//   if (decodeMitsubishi(results)) {
//     return DECODED;
//   }

// #ifdef DEBUG_IR
//   Serial.println("Attempting RC5 decode");
// #endif
//   if (decodeRC5(results)) {
//     return DECODED;
//   }

// #ifdef DEBUG_IR
//   Serial.println("Attempting RC6 decode");
// #endif
//   if (decodeRC6(results)) {
//     return DECODED;
//   }

// #ifdef DEBUG_IR
//   Serial.println("Attempting JVC decode");
// #endif
//   if (decodeJVC(results)) {
//     return DECODED;
//   }

// #ifdef DEBUG_IR
//   Serial.println("Attempting DISNEY decode");
// #endif
//   if (decodeDisney(results)) {
//     return DECODED;
//   }

    // decodeHash returns a hash on any input.
    // Thus, it needs to be last in the list.
    // If you add any decodes, add them before this.
    // if (decodeHash(results)) {
    //     return DECODED;
    // }

    // Throw away and start over
    resume();
    return ERR;
}

long IRrecv::decodeDisney(decode_results *results) {
    int bits;
    int total_bits = 0;
    uint8_t temp_byte = 0;
    results->rx_len = 0;

    // Raw (99): 498,-298,1344,-302,946,-724,890,-1150,1744,-298,1318,-1146,1348,-298,498,-324,1744,-750,494,
    // 91 0E 16 1F

    // Attempting DISNEY decode
    // 988,9,8,23,9,16,16,16,24,32,8,24,25,24,8,8,8,32,17,7,
    // Attempting DISNEY decode
    // 246345,9,6,27,6,17,15,17,23,34,7,24,23,26,7,9,7,33,16,9,
    //        1,1, 3,1, 2, 2, 2, 3, 4,1, 3, 3, 3,1,1,1, 4, 2,1,
    // 0 10001001 10 01110000 10 00111000 10 10000110

    // :3d,H1,L2,H2,L5,:06,H1,L1,H1,L1,H2,L2,

    // Require at least 6 samples to prevent triggering on noise
    if (results->rawlen < 6) {
        return ERR;
    }

    // for (int i=1; i < results->rawlen; i++) {
    //     bits = (int)round(float(results->rawbuf[i])/417.0f);
    //     Serial.print(bits);
    Serial.print(",");
    // }
    // Serial.println();

    for (int i = 0; i < results->rawlen; i++) {
        // Skip over junk data
        // TODO: Fix ir_recv_handler sometimes returns [12,-UINT_MAX,...,...]
        if (results->rawbuf[i] < 50 || results->rawbuf[i] > 500000) {
            continue;
        }

        bits = (int)round(float(results->rawbuf[i]) / 417.0f); // 1/2400baud = 417us per bit
#ifdef DEBUG_IR
        // Serial.printf("%s%d=", (i&1)?"H":"L", bits);
#endif

        // Process time slice of bits
        while (bits > 0) {
            // MARK (low input is low bit, usually inverted)
            if (!(i & 1)) {
                if (total_bits < 9) {
                    if (total_bits != 0) {
                        temp_byte &= ~(1 << total_bits - 1); // clear bit
#ifdef DEBUG_IR
                        Serial.print("v");
#endif
                    } else {
#ifdef DEBUG_IR
                        Serial.print("s"); // start bit
#endif
                    }
                    total_bits++;
                    bits--;
                }
            }
            // SPACE (high input is high bit, usually inverted)
            else {
                if (total_bits == 0) {
                    while (bits > 0) {
                        // this effectively removes trailing idle time
                        bits--;
#ifdef DEBUG_IR
                        Serial.print("o");
#endif
                    }
                } else if (total_bits < 9) {
                    if (total_bits != 0) {
                        temp_byte |= (1 << total_bits - 1); // set bit
#ifdef DEBUG_IR
                        Serial.print("^");
#endif
                    }
                    total_bits++;
                    bits--;
                    if (total_bits == 9) {
                        while (bits > 0) {
                            // this effectively removes trailing idle time
                            bits--;
#ifdef DEBUG_IR
                            Serial.print("x");
#endif
                        }
                    }
                }
            }

            // if we have processed the last MARK, pad the last byte with HIGH data
            if (bits == 0 && total_bits < 9) {
                if (i == results->rawlen - 1) {
                    while (total_bits < 9) {
                        temp_byte |= (1 << total_bits - 1); // set bit
#ifdef DEBUG_IR
                        Serial.print("^");
#endif
                        total_bits++;
                    }
                }
            }

            // Process byte
            if (total_bits == 9) {
                results->rx_data[results->rx_len++] = temp_byte;
#ifdef DEBUG_IR
                Serial.printf(":%02x,", temp_byte);
#endif
                total_bits = 0;
            }
        } // while (bits)
    }
#ifdef DEBUG_IR
    Serial.println();
#endif

    if (total_bits != 0) {
        // Serial.println("error");
        return ERR;
    }

    results->value = 0;
    results->bits = results->rx_len * 8;
    results->decode_type = DISNEY;
    return DECODED;
}

// NECs have a repeat only 4 items long
long IRrecv::decodeBytes(decode_results *results) {
    uint8_t data = 0;
    int offset = 0; // Skip first space
    results->rx_len = 0;

    // Require at least 6 samples to prevent triggering on noise
    if (results->rawlen < 80) {
        return ERR;
    }

    // IR HEADER capture START
    // Initial mark
    if (!MATCH_MARK(results->rawbuf[offset], NEC_HDR_MARK)) {
        return ERR;
    }
    offset++;
    // // Check for repeat
    // if (irparams.rawlen == 4 &&
    //         MATCH_SPACE(results->rawbuf[offset], NEC_RPT_SPACE) &&
    //         MATCH_MARK(results->rawbuf[offset + 1], NEC_BIT_MARK)) {
    //     results->bits = 0;
    //     results->value = REPEAT;
    //     results->decode_type = NEC;
    //     return DECODED;
    // }
    // if (irparams.rawlen < 2 * NEC_BITS + 3) {
    //     return ERR;
    // }

    // Initial space
    if (!MATCH_SPACE(results->rawbuf[offset], NEC_HDR_SPACE)) {
        return ERR;
    }
    offset++;
    // IR HEADER capture END

    // Receive LEN, DATA, and CRC
    int total_len = ((results->rawlen - offset - 1)/8/2);
    // Serial.printlnf("TOTAL LEN:%d", total_len);
    for (int x = 0; x < total_len; x++) {
        for (int y = 0; y < 8; y++) {
            if (!MATCH_MARK(results->rawbuf[offset], NEC_BIT_MARK)) {
                return ERR;
            }
            offset++;
            if (MATCH_SPACE(results->rawbuf[offset], NEC_ONE_SPACE)) {
                data = (data << 1) | 1;
            }
            else if (MATCH_SPACE(results->rawbuf[offset], NEC_ZERO_SPACE)) {
                data <<= 1;
            }
            else {
                return ERR;
            }
            offset++;
        }
        results->rx_data[results->rx_len++] = data;
        if (x == 0) {
            Serial.printf("LEN:%d,", data);
        }
        // else if (x == 0) {
        //     Serial.printf("LEN:%02x,", data);
        // }
        else {
            Serial.printf("%02x,", data);
        }
        data = 0;
    }
    Serial.println("");

    // Validate CRC
    uint8_t len = results->rx_data[0];
    uint8_t crc_sent = results->rx_data[len-1];
    uint8_t crc_bytes = crc8(&results->rx_data[1], len-2);
    if (crc_bytes != crc_sent) {
        Serial.println("Bad CRC!");
        return ERR;
    }

    // Success
    results->value = 0;
    results->bits = results->rx_len * 8;
    results->decode_type = BYTES;
    return DECODED;
}

// NECs have a repeat only 4 items long
long IRrecv::decodeNEC(decode_results *results) {
    long data = 0;
    int offset = 0; // Skip first space
    // Initial mark
    if (!MATCH_MARK(results->rawbuf[offset], NEC_HDR_MARK)) {
        return ERR;
    }
    offset++;
    // Check for repeat
    if (irparams.rawlen == 4 &&
            MATCH_SPACE(results->rawbuf[offset], NEC_RPT_SPACE) &&
            MATCH_MARK(results->rawbuf[offset + 1], NEC_BIT_MARK)) {
        results->bits = 0;
        results->value = REPEAT;
        results->decode_type = NEC;
        return DECODED;
    }
    if (irparams.rawlen < 2 * NEC_BITS + 3) {
        return ERR;
    }
    // Initial space
    if (!MATCH_SPACE(results->rawbuf[offset], NEC_HDR_SPACE)) {
        return ERR;
    }
    offset++;
    for (int i = 0; i < NEC_BITS; i++) {
        if (!MATCH_MARK(results->rawbuf[offset], NEC_BIT_MARK)) {
            return ERR;
        }
        offset++;
        if (MATCH_SPACE(results->rawbuf[offset], NEC_ONE_SPACE)) {
            data = (data << 1) | 1;
        }
        else if (MATCH_SPACE(results->rawbuf[offset], NEC_ZERO_SPACE)) {
            data <<= 1;
        }
        else {
            return ERR;
        }
        offset++;
    }
    // Success
    results->bits = NEC_BITS;
    results->value = data;
    results->decode_type = NEC;
    return DECODED;
}

long IRrecv::decodeSony(decode_results *results) {
    long data = 0;
    if (irparams.rawlen < 2 * SONY_BITS + 2) {
        return ERR;
    }
    int offset = 0; // Dont skip first space, check its size

    // Some Sony's deliver repeats fast after first
    // unfortunately can't spot difference from of repeat from two fast clicks
    if (results->rawbuf[offset] < SONY_DOUBLE_SPACE_USECS) {
        // Serial.print("IR Gap found: ");
        results->bits = 0;
        results->value = REPEAT;
        results->decode_type = SANYO;
        return DECODED;
    }
    // offset++;

    // Initial mark
    if (!MATCH_MARK(results->rawbuf[offset], SONY_HDR_MARK)) {
        return ERR;
    }
    offset++;

    while ((offset + 1) < irparams.rawlen) {
        if (!MATCH_SPACE(results->rawbuf[offset], SONY_HDR_SPACE)) {
            break;
        }
        offset++;
        if (MATCH_MARK(results->rawbuf[offset], SONY_ONE_MARK)) {
            data = (data << 1) | 1;
        }
        else if (MATCH_MARK(results->rawbuf[offset], SONY_ZERO_MARK)) {
            data <<= 1;
        }
        else {
            return ERR;
        }
        offset++;
    }

    // Success
    results->bits = (offset - 1) / 2;
    if (results->bits < 12) {
        results->bits = 0;
        return ERR;
    }
    results->value = data;
    results->decode_type = SONY;
    return DECODED;
}

// I think this is a Sanyo decoder - serial = SA 8650B
// Looks like Sony except for timings, 48 chars of data and time/space different
long IRrecv::decodeSanyo(decode_results *results) {
    long data = 0;
    if (irparams.rawlen < 2 * SANYO_BITS + 2) {
        return ERR;
    }
    int offset = 0; // Skip first space
    // Initial space
    /* Put this back in for debugging - note can't use #DEBUG as if Debug on we don't see the repeat cos of the delay
    Serial.print("IR Gap: ");
    Serial.println( results->rawbuf[offset]);
    Serial.println( "test against:");
    Serial.println(results->rawbuf[offset]);
    */
    if (results->rawbuf[offset] < SANYO_DOUBLE_SPACE_USECS) {
        // Serial.print("IR Gap found: ");
        results->bits = 0;
        results->value = REPEAT;
        results->decode_type = SANYO;
        return DECODED;
    }
    offset++;

    // Initial mark
    if (!MATCH_MARK(results->rawbuf[offset], SANYO_HDR_MARK)) {
        return ERR;
    }
    offset++;

    // Skip Second Mark
    if (!MATCH_MARK(results->rawbuf[offset], SANYO_HDR_MARK)) {
        return ERR;
    }
    offset++;

    while ((offset + 1) < irparams.rawlen) {
        if (!MATCH_SPACE(results->rawbuf[offset], SANYO_HDR_SPACE)) {
            break;
        }
        offset++;
        if (MATCH_MARK(results->rawbuf[offset], SANYO_ONE_MARK)) {
            data = (data << 1) | 1;
        }
        else if (MATCH_MARK(results->rawbuf[offset], SANYO_ZERO_MARK)) {
            data <<= 1;
        }
        else {
            return ERR;
        }
        offset++;
    }

    // Success
    results->bits = (offset - 1) / 2;
    if (results->bits < 12) {
        results->bits = 0;
        return ERR;
    }
    results->value = data;
    results->decode_type = SANYO;
    return DECODED;
}

// Looks like Sony except for timings, 48 chars of data and time/space different
long IRrecv::decodeMitsubishi(decode_results *results) {
    // Serial.print("?!? decoding Mitsubishi:");Serial.print(irparams.rawlen); Serial.print(" want "); Serial.println( 2 * MITSUBISHI_BITS + 2);
    long data = 0;
    if (irparams.rawlen < 2 * MITSUBISHI_BITS + 2) {
        return ERR;
    }
    int offset = 0; // Skip first space
    // Initial space
    /* Put this back in for debugging - note can't use #DEBUG as if Debug on we don't see the repeat cos of the delay
    Serial.print("IR Gap: ");
    Serial.println( results->rawbuf[offset]);
    Serial.println( "test against:");
    Serial.println(results->rawbuf[offset]);
    */
    /* Not seeing double keys from Mitsubishi
    if (results->rawbuf[offset] < MITSUBISHI_DOUBLE_SPACE_USECS) {
      // Serial.print("IR Gap found: ");
      results->bits = 0;
      results->value = REPEAT;
      results->decode_type = MITSUBISHI;
      return DECODED;
    }
    */
    offset++;

    // Typical
    // 14200 7 41 7 42 7 42 7 17 7 17 7 18 7 41 7 18 7 17 7 17 7 18 7 41 8 17 7 17 7 18 7 17 7

    // Initial Space
    if (!MATCH_MARK(results->rawbuf[offset], MITSUBISHI_HDR_SPACE)) {
        return ERR;
    }
    offset++;
    while ((offset + 1) < irparams.rawlen) {
        if (MATCH_MARK(results->rawbuf[offset], MITSUBISHI_ONE_MARK)) {
            data = (data << 1) | 1;
        }
        else if (MATCH_MARK(results->rawbuf[offset], MITSUBISHI_ZERO_MARK)) {
            data <<= 1;
        }
        else {
            // Serial.println("A"); Serial.println(offset); Serial.println(results->rawbuf[offset]);
            return ERR;
        }
        offset++;
        if (!MATCH_SPACE(results->rawbuf[offset], MITSUBISHI_HDR_SPACE)) {
            // Serial.println("B"); Serial.println(offset); Serial.println(results->rawbuf[offset]);
            break;
        }
        offset++;
    }

    // Success
    results->bits = (offset - 1) / 2;
    if (results->bits < MITSUBISHI_BITS) {
        results->bits = 0;
        return ERR;
    }
    results->value = data;
    results->decode_type = MITSUBISHI;
    return DECODED;
}


// Gets one undecoded level at a time from the raw buffer.
// The RC5/6 decoding is easier if the data is broken into time intervals.
// E.g. if the buffer has MARK for 2 time intervals and SPACE for 1,
// successive calls to getRClevel will return MARK, MARK, SPACE.
// offset and used are updated to keep track of the current position.
// t1 is the time interval for a single bit in microseconds.
// Returns -1 for error (measured time interval is not a multiple of t1).
int IRrecv::getRClevel(decode_results *results, int *offset, int *used, int t1) {
    if (*offset >= results->rawlen) {
        // After end of recorded buffer, assume SPACE.
        return SPACE;
    }
    int width = results->rawbuf[*offset];
    int val = ((*offset) % 2) ? MARK : SPACE;
    int correction = (val == MARK) ? MARK_EXCESS : - MARK_EXCESS;

    int avail;
    if (MATCH(width, t1 + correction)) {
        avail = 1;
    }
    else if (MATCH(width, 2 * t1 + correction)) {
        avail = 2;
    }
    else if (MATCH(width, 3 * t1 + correction)) {
        avail = 3;
    }
    else {
        return -1;
    }

    (*used)++;
    if (*used >= avail) {
        *used = 0;
        (*offset)++;
    }
#ifdef DEBUG_IR
    if (val == MARK) {
        Serial.println("MARK");
    }
    else {
        Serial.println("SPACE");
    }
#endif
    return val;
}

long IRrecv::decodeRC5(decode_results *results) {
    if (irparams.rawlen < MIN_RC5_SAMPLES + 2) {
        return ERR;
    }
    int offset = 0; // Skip gap space
    long data = 0;
    int used = 0;
    // Get start bits
    if (getRClevel(results, &offset, &used, RC5_T1) != MARK) return ERR;
    if (getRClevel(results, &offset, &used, RC5_T1) != SPACE) return ERR;
    if (getRClevel(results, &offset, &used, RC5_T1) != MARK) return ERR;
    int nbits;
    for (nbits = 0; offset < irparams.rawlen; nbits++) {
        int levelA = getRClevel(results, &offset, &used, RC5_T1);
        int levelB = getRClevel(results, &offset, &used, RC5_T1);
        if (levelA == SPACE && levelB == MARK) {
            // 1 bit
            data = (data << 1) | 1;
        }
        else if (levelA == MARK && levelB == SPACE) {
            // zero bit
            data <<= 1;
        }
        else {
            return ERR;
        }
    }

    // Success
    results->bits = nbits;
    results->value = data;
    results->decode_type = RC5;
    return DECODED;
}

long IRrecv::decodeRC6(decode_results *results) {
    if (results->rawlen < MIN_RC6_SAMPLES) {
        return ERR;
    }
    int offset = 0; // Skip first space
    // Initial mark
    if (!MATCH_MARK(results->rawbuf[offset], RC6_HDR_MARK)) {
        return ERR;
    }
    offset++;
    if (!MATCH_SPACE(results->rawbuf[offset], RC6_HDR_SPACE)) {
        return ERR;
    }
    offset++;
    long data = 0;
    int used = 0;
    // Get start bit (1)
    if (getRClevel(results, &offset, &used, RC6_T1) != MARK) return ERR;
    if (getRClevel(results, &offset, &used, RC6_T1) != SPACE) return ERR;
    int nbits;
    for (nbits = 0; offset < results->rawlen; nbits++) {
        int levelA, levelB; // Next two levels
        levelA = getRClevel(results, &offset, &used, RC6_T1);
        if (nbits == 3) {
            // T bit is double wide; make sure second half matches
            if (levelA != getRClevel(results, &offset, &used, RC6_T1)) return ERR;
        }
        levelB = getRClevel(results, &offset, &used, RC6_T1);
        if (nbits == 3) {
            // T bit is double wide; make sure second half matches
            if (levelB != getRClevel(results, &offset, &used, RC6_T1)) return ERR;
        }
        if (levelA == MARK && levelB == SPACE) { // reversed compared to RC5
            // 1 bit
            data = (data << 1) | 1;
        }
        else if (levelA == SPACE && levelB == MARK) {
            // zero bit
            data <<= 1;
        }
        else {
            return ERR; // Error
        }
    }
    // Success
    results->bits = nbits;
    results->value = data;
    results->decode_type = RC6;
    return DECODED;
}
long IRrecv::decodePanasonic(decode_results *results) {
    unsigned long long data = 0;
    int offset = 0;

    if (!MATCH_MARK(results->rawbuf[offset], PANASONIC_HDR_MARK)) {
        return ERR;
    }
    offset++;
    if (!MATCH_MARK(results->rawbuf[offset], PANASONIC_HDR_SPACE)) {
        return ERR;
    }
    offset++;

    // decode address
    for (int i = 0; i < PANASONIC_BITS; i++) {
        if (!MATCH_MARK(results->rawbuf[offset++], PANASONIC_BIT_MARK)) {
            return ERR;
        }
        if (MATCH_SPACE(results->rawbuf[offset], PANASONIC_ONE_SPACE)) {
            data = (data << 1) | 1;
        } else if (MATCH_SPACE(results->rawbuf[offset], PANASONIC_ZERO_SPACE)) {
            data <<= 1;
        } else {
            return ERR;
        }
        offset++;
    }
    results->value = (unsigned long)data;
    results->address = (unsigned int)(data >> 32);
    results->decode_type = PANASONIC;
    results->bits = PANASONIC_BITS;
    return DECODED;
}
long IRrecv::decodeJVC(decode_results *results) {
    long data = 0;
    int offset = 0; // Skip first space
    // Check for repeat
    if (irparams.rawlen - 1 == 33 &&
            MATCH_MARK(results->rawbuf[offset], JVC_BIT_MARK) &&
            MATCH_MARK(results->rawbuf[irparams.rawlen - 1], JVC_BIT_MARK)) {
        results->bits = 0;
        results->value = REPEAT;
        results->decode_type = JVC;
        return DECODED;
    }
    // Initial mark
    if (!MATCH_MARK(results->rawbuf[offset], JVC_HDR_MARK)) {
        return ERR;
    }
    offset++;
    if (irparams.rawlen < 2 * JVC_BITS + 1 ) {
        return ERR;
    }
    // Initial space
    if (!MATCH_SPACE(results->rawbuf[offset], JVC_HDR_SPACE)) {
        return ERR;
    }
    offset++;
    for (int i = 0; i < JVC_BITS; i++) {
        if (!MATCH_MARK(results->rawbuf[offset], JVC_BIT_MARK)) {
            return ERR;
        }
        offset++;
        if (MATCH_SPACE(results->rawbuf[offset], JVC_ONE_SPACE)) {
            data = (data << 1) | 1;
        }
        else if (MATCH_SPACE(results->rawbuf[offset], JVC_ZERO_SPACE)) {
            data <<= 1;
        }
        else {
            return ERR;
        }
        offset++;
    }
    //Stop bit
    if (!MATCH_MARK(results->rawbuf[offset], JVC_BIT_MARK)) {
        return ERR;
    }
    // Success
    results->bits = JVC_BITS;
    results->value = data;
    results->decode_type = JVC;
    return DECODED;
}

/* -----------------------------------------------------------------------
 * hashdecode - decode an arbitrary IR code.
 * Instead of decoding using a standard encoding scheme
 * (e.g. Sony, NEC, RC5), the code is hashed to a 32-bit value.
 *
 * The algorithm: look at the sequence of MARK signals, and see if each one
 * is shorter (0), the same length (1), or longer (2) than the previous.
 * Do the same with the SPACE signals.  Hszh the resulting sequence of 0's,
 * 1's, and 2's to a 32-bit value.  This will give a unique value for each
 * different code (probably), for most code systems.
 *
 * http://arcfn.com/2010/01/using-arbitrary-remotes-with-arduino.html
 */

// Compare two tick values, returning 0 if newval is shorter,
// 1 if newval is equal, and 2 if newval is longer
// Use a tolerance of 20%
int IRrecv::compare(unsigned int oldval, unsigned int newval) {
    if (newval < oldval * .8) {
        return 0;
    }
    else if (oldval < newval * .8) {
        return 2;
    }
    else {
        return 1;
    }
}

// Use FNV hash algorithm: http://isthe.com/chongo/tech/comp/fnv/#FNV-param
#define FNV_PRIME_32 16777619
#define FNV_BASIS_32 2166136261

/* Converts the raw code values into a 32-bit hash code.
 * Hopefully this code is unique for each button.
 * This isn't a "real" decoding, just an arbitrary value.
 */
long IRrecv::decodeHash(decode_results *results) {
    // Require at least 6 samples to prevent triggering on noise
    if (results->rawlen < 6) {
        return ERR;
    }
    long hash = FNV_BASIS_32;
    for (int i = 1; i + 2 < results->rawlen; i++) {
        int value =  compare(results->rawbuf[i], results->rawbuf[i + 2]);
        // Add value into the hash
        hash = (hash * FNV_PRIME_32) ^ value;
    }
    results->value = hash;
    results->bits = 32;
    results->decode_type = PROTOCOL_UNKNOWN;
    return DECODED;
}

/* Sharp and DISH support by Todd Treece ( http://unionbridge.org/design/ircommand )

The Dish send function needs to be repeated 4 times, and the Sharp function
has the necessary repeat built in because of the need to invert the signal.

Sharp protocol documentation:
http://www.sbprojects.com/knowledge/ir/sharp.htm

Here are the LIRC files that I found that seem to match the remote codes
from the oscilloscope:

Sharp LCD TV:
http://lirc.sourceforge.net/remotes/sharp/GA538WJSA

DISH NETWORK (echostar 301):
http://lirc.sourceforge.net/remotes/echostar/301_501_3100_5100_58xx_59xx

For the DISH codes, only send the last for characters of the hex.
i.e. use 0x1C10 instead of 0x0000000000001C10 which is listed in the
linked LIRC file.
*/

void IRsend::sendSharp(unsigned long data, int nbits) {
    unsigned long invertdata = data ^ SHARP_TOGGLE_MASK;
    enableIROut(38);
    for (int i = 0; i < nbits; i++) {
        if (data & 0x4000) {
            mark(SHARP_BIT_MARK);
            space(SHARP_ONE_SPACE);
        }
        else {
            mark(SHARP_BIT_MARK);
            space(SHARP_ZERO_SPACE);
        }
        data <<= 1;
    }

    mark(SHARP_BIT_MARK);
    space(SHARP_ZERO_SPACE);
    delay(46);
    for (int i = 0; i < nbits; i++) {
        if (invertdata & 0x4000) {
            mark(SHARP_BIT_MARK);
            space(SHARP_ONE_SPACE);
        }
        else {
            mark(SHARP_BIT_MARK);
            space(SHARP_ZERO_SPACE);
        }
        invertdata <<= 1;
    }
    mark(SHARP_BIT_MARK);
    space(SHARP_ZERO_SPACE);
    delay(46);
}

void IRsend::sendDISH(unsigned long data, int nbits)
{
    enableIROut(56);
    mark(DISH_HDR_MARK);
    space(DISH_HDR_SPACE);
    for (int i = 0; i < nbits; i++) {
        if (data & DISH_TOP_BIT) {
            mark(DISH_BIT_MARK);
            space(DISH_ONE_SPACE);
        }
        else {
            mark(DISH_BIT_MARK);
            space(DISH_ZERO_SPACE);
        }
        data <<= 1;
    }
}
