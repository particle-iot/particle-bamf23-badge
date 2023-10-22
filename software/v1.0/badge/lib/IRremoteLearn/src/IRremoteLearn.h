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
 * For details, see http://arcfn.com/2009/08/multi-protocol-infrared-remote-library.htm http://arcfn.com
 * Edited by Mitra to add new controller SANYO
 *
 * Interrupt code based on NECIRrcv by Joe Knapp
 * http://www.arduino.cc/cgi-bin/yabb2/YaBB.pl?num=1210243556
 * Also influenced by http://zovirl.com/2008/11/12/building-a-universal-remote-with-an-arduino/
 *
 * JVC and Panasonic protocol added by Kristian Lauszus (Thanks to zenwheel and other people at the original blog post)
 */

#ifndef IRremoteLearn_h
#define IRremoteLearn_h

#include "Particle.h"

// The following are compile-time library options.
// If you change them, recompile the library.
// If DEBUG_IR is defined, a lot of debugging output will be printed during decoding.

// #define DEBUG_IR

// Results returned from the decoder
class decode_results {
public:
  int decode_type;                // NEC, SONY, RC5, UNKNOWN
  unsigned int address;           // This is only used for decoding Panasonic data
  unsigned long value;            // Decoded value
  int bits;                       // Number of bits in decoded value
  volatile unsigned long *rawbuf; // Raw intervals in .5 us ticks
  unsigned long rawlen;           // Number of records in rawbuf.
  uint8_t rx_data[100];           // Receive data buffer for longer protocols
  uint16_t rx_len;                // Receive data buffer length for longer protocols
};

// Values for decode_type
#define NEC 1
#define SONY 2
#define RC5 3
#define RC6 4
#define DISH 5
#define SHARP 6
#define PANASONIC 7
#define JVC 8
#define SANYO 9
#define MITSUBISHI 10
#define DISNEY 11
#define BYTES 12
#define PROTOCOL_UNKNOWN -1

// Decoded value for NEC when a repeat code is received
#define REPEAT 0xffffffff

// main class for receiving IR
class IRrecv
{
public:
  IRrecv(int rxpin, unsigned long idle_timout_ms=300, unsigned long mark_timout_us=200);
  void blink13(int blinkflag);
  int decode(decode_results *results);
  void enableIRIn();
  void disableIRIn();
  void resume();
private:
  // These are called by decode
  int getRClevel(decode_results *results, int *offset, int *used, int t1);
  long decodeNEC(decode_results *results);
  long decodeSony(decode_results *results);
  long decodeSanyo(decode_results *results);
  long decodeMitsubishi(decode_results *results);
  long decodeRC5(decode_results *results);
  long decodeRC6(decode_results *results);
  long decodePanasonic(decode_results *results);
  long decodeJVC(decode_results *results);
  long decodeHash(decode_results *results);
  long decodeDisney(decode_results *results);
  long decodeBytes(decode_results *results);
  int compare(unsigned int oldval, unsigned int newval);

};

class IRsend
{
public:
  IRsend(int txpin=TX); // TX pin is used by default
  void sendNEC(unsigned long data, int nbits);
  void sendSony(unsigned long data, int nbits);
  // Neither Sanyo nor Mitsubishi send is implemented yet
  //  void sendSanyo(unsigned long data, int nbits);
  //  void sendMitsubishi(unsigned long data, int nbits);
  void sendRaw(unsigned int buf[], int len, int hz);
  void sendRC5(unsigned long data, int nbits);
  void sendRC6(unsigned long data, int nbits);
  void sendDISH(unsigned long data, int nbits);
  void sendSharp(unsigned long data, int nbits);
  void sendPanasonic(unsigned int address, unsigned long data);
  void sendJVC(unsigned long data, int nbits, int repeat); // *Note instead of sending the REPEAT constant if you want the JVC repeat signal sent, send the original code value and change the repeat argument from 0 to 1. JVC protocol repeats by skipping the header NOT by sending a separate code value like NEC does.
  void sendBytes(uint8_t data[], int len);
  // private:
  void enableIROut(int khz);
  void mark(int usec);
  void space(int usec);
};

// Some useful constants

#define USECPERTICK 1 // microseconds per clock interrupt tick (we are capturing times in microseconds to 1:1)
#define RAWBUF 512 // Length of raw rx duration buffer
#define TX_BUF_MAX 32 // Length of tx buffer

// Marks tend to be 100us too long, and spaces 100us too short
// when received due to sensor lag.
#define MARK_EXCESS 1

#endif // IRremoteLearn_h
