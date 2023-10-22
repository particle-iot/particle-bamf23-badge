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
 * Version 0.1 July, 2009
 * Copyright 2009 Ken Shirriff
 * For details, see http://arcfn.com/2009/08/multi-protocol-infrared-remote-library.html
 *
 * Modified by Paul Stoffregen <paul@pjrc.com> to support other boards and timers
 *
 * Interrupt code based on NECIRrcv by Joe Knapp
 * http://www.arduino.cc/cgi-bin/yabb2/YaBB.pl?num=1210243556
 * Also influenced by http://zovirl.com/2008/11/12/building-a-universal-remote-with-an-arduino/
 *
 * JVC and Panasonic protocol added by Kristian Lauszus (Thanks to zenwheel and other people at the original blog post)
 */

#ifndef IRremoteLearnInt_h
#define IRremoteLearnInt_h

#include "Particle.h"

// #define DEBUG_IR 1

#define ERR 0
#define DECODED 1

// Pulse parms are *50-100 for the Mark and *50+100 for the space
// First MARK is the one after the long gap
// pulse parameters in usec
// #define NEC_HDR_MARK	9000
// #define NEC_HDR_SPACE	4500
// #define NEC_BIT_MARK	560
// #define NEC_ONE_SPACE	1600
// #define NEC_ZERO_SPACE	560
#define NEC_HDR_MARK  4000  // tweaked shorter for the TSOP32338
#define NEC_HDR_SPACE 2000
#define NEC_BIT_MARK  500
#define NEC_ONE_SPACE 1000
#define NEC_ZERO_SPACE 500
#define NEC_RPT_SPACE	2250

#define SONY_HDR_MARK	2400
#define SONY_HDR_SPACE	600
#define SONY_ONE_MARK	1200
#define SONY_ZERO_MARK	600
#define SONY_RPT_LENGTH 45000
#define SONY_DOUBLE_SPACE_USECS  500  // usually ssee 713 - not using ticks as get number wrapround

// SA 8650B
#define SANYO_HDR_MARK	3500  // seen range 3500
#define SANYO_HDR_SPACE	950 //  seen 950
#define SANYO_ONE_MARK	2400 // seen 2400
#define SANYO_ZERO_MARK 700 //  seen 700
#define SANYO_DOUBLE_SPACE_USECS  800  // usually ssee 713 - not using ticks as get number wrapround
#define SANYO_RPT_LENGTH 45000

// Mitsubishi RM 75501
// 14200 7 41 7 42 7 42 7 17 7 17 7 18 7 41 7 18 7 17 7 17 7 18 7 41 8 17 7 17 7 18 7 17 7

// #define MITSUBISHI_HDR_MARK	250  // seen range 3500
#define MITSUBISHI_HDR_SPACE	350 //  7*50+100
#define MITSUBISHI_ONE_MARK	1950 // 41*50-100
#define MITSUBISHI_ZERO_MARK  750 // 17*50-100
// #define MITSUBISHI_DOUBLE_SPACE_USECS  800  // usually ssee 713 - not using ticks as get number wrapround
// #define MITSUBISHI_RPT_LENGTH 45000


#define RC5_T1		889
#define RC5_RPT_LENGTH	46000

#define RC6_HDR_MARK	2666
#define RC6_HDR_SPACE	889
#define RC6_T1		444
#define RC6_RPT_LENGTH	46000

#define SHARP_BIT_MARK 245
#define SHARP_ONE_SPACE 1805
#define SHARP_ZERO_SPACE 795
#define SHARP_GAP 600000
#define SHARP_TOGGLE_MASK 0x3FF
#define SHARP_RPT_SPACE 3000

#define DISH_HDR_MARK 400
#define DISH_HDR_SPACE 6100
#define DISH_BIT_MARK 400
#define DISH_ONE_SPACE 1700
#define DISH_ZERO_SPACE 2800
#define DISH_RPT_SPACE 6200
#define DISH_TOP_BIT 0x8000

#define PANASONIC_HDR_MARK 3500
#define PANASONIC_HDR_SPACE 1775
#define PANASONIC_BIT_MARK 432
#define PANASONIC_ONE_SPACE 1296
#define PANASONIC_ZERO_SPACE 453

#define JVC_HDR_MARK 8000
#define JVC_HDR_SPACE 4000
#define JVC_BIT_MARK 600
#define JVC_ONE_SPACE 1600
#define JVC_ZERO_SPACE 550
#define JVC_RPT_LENGTH 60000

#define SHARP_BITS 15
#define DISH_BITS 16

#define TOLERANCE 25  // percent tolerance in measurements
#define LTOL (1.0 - TOLERANCE/100.)
#define UTOL (1.0 + TOLERANCE/100.)

#define TICKS_LOW(us) (int) (((us)*LTOL/USECPERTICK))
#define TICKS_HIGH(us) (int) (((us)*UTOL/USECPERTICK + 1))

#ifndef DEBUG_IR
int MATCH(int measured, int desired) {return measured >= TICKS_LOW(desired) && measured <= TICKS_HIGH(desired);}
int MATCH_MARK(int measured_ticks, int desired_us) {return MATCH(measured_ticks, (desired_us + MARK_EXCESS));}
int MATCH_SPACE(int measured_ticks, int desired_us) {return MATCH(measured_ticks, (desired_us - MARK_EXCESS));}
// Debugging versions are in IRremoteLearn.cpp
#endif

// receiver states
#define STATE_IDLE     2
#define STATE_MARK     3
#define STATE_SPACE    4
#define STATE_STOP     5
#define STATE_CAPTURED 6

// information for the interrupt handler
typedef struct {
  uint8_t rxpin;                 // pin for IR rx data from detector
  uint8_t txpin;                 // pin for IR tx data from detector
  uint8_t rcvstate;              // state machine
  uint8_t blinkflag;             // TRUE to enable blinking of pin 13 on IR processing
  // unsigned int timer;         // state timer, counts 50uS ticks.
  unsigned long rawbuf1[RAWBUF]; // raw data 1 (double buffered to receive while decoding)
  unsigned long rawbuf2[RAWBUF]; // raw data 2
  unsigned long rawlen;          // counter of entries in rawbuf
  unsigned long current_time;    // current time in micro seconds
  unsigned long start_time;      // start time in micro seconds
  unsigned long end_time;        // end time in micro seconds
  int irout_khz;                 // frequency used for PWM output
  unsigned long idle_timout_ms;  // idle timeout in milliseconds
  unsigned long mark_timout_us;  // mark timeout in microseconds
  uint8_t txbuf[TX_BUF_MAX];     // temporary TX buffer for sendBytes()
}
irparams_t;

// Defined in IRremoteLearn.cpp
extern volatile irparams_t irparams;

// IR detector output is active low
#define MARK  0
#define SPACE 1

#define TOPBIT 0x80000000

#define NEC_BITS 32
#define SONY_BITS 12
#define SANYO_BITS 12
#define MITSUBISHI_BITS 16
#define MIN_RC5_SAMPLES 11
#define MIN_RC6_SAMPLES 1
#define PANASONIC_BITS 48
#define JVC_BITS 16

#define BLINKLED       D7
#define BLINKLED_ON()  digitalWriteFast(BLINKLED,1)
#define BLINKLED_OFF() digitalWriteFast(BLINKLED,0)

#endif // IRremoteLearnInt_h
