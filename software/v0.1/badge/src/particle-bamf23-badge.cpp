/*
 * Particle Bay Area Maker Faire 2023 Badge
 *
 * by Brett Walach ( @Technobly / github.com/technobly )
 * July 30th, 2023
 *
 */

#include "Particle.h"
#include "IRremoteLearn.h"
#include "neopixel.h"
// #include "adafruit-sht31.h"
// Adafruit_SHT31 sht31 = Adafruit_SHT31();

SYSTEM_MODE(SEMI_AUTOMATIC);

// MOSI pin MO
#define PIXEL_PIN SPI
#define PIXEL_COUNT 7
#define PIXEL_TYPE WS2812B

const int BUTTON_1_PIN = S4;
const int BUTTON_2_PIN = S3;
const int BUTTON_3_PIN = D6;
const int BUTTON_4_PIN = D5;
const int PIXEL_ENABLE_PIN = D4;
const int SPEAKER_PIN = A2;
const int IR_TX_PIN = A5;
const int IR_RX_PIN = D3;

Adafruit_NeoPixel strip(PIXEL_COUNT, PIXEL_PIN, PIXEL_TYPE);

decode_results irResults;
system_tick_t lastSleep = 0;
int wakeupReason = 0;
int wakeupPin = 0;
int colorPick = 0;


// ATTACK (P1): HEADER:P1_ID:[MSG_TYP:MSG_BTN:MSG_STR]:P1_SCORE:CRC
//              1:4:1:2:1
//
// COUNTER(P2): HEADER:P2_ID:[MSG_TYP:MSG_BTN:MSG_STR]:P2_SCORE:P1_ID:[MSG_TYP:MSG_BTN:MSG_STR]:CRC
//              1:4:1:2:4:1
//
// RESULT (P1): HEADER:P1_ID:[MSG_TYP:MSG_BTN:MSG_STR]:P1_P2_ID:CRC
//              1:4:1:4:1
//
// SCORE_ACK(P2): HEADER:P1_ID:[MSG_TYP:MSG_BTN:MSG_STR]:P1_SCORE:CRC
//                1:4:1:2:1
//
#define MAGIC_HEADER_BYTE           (0xA2)
#define MESSAGE_TYPE_MASK           (0x60)
#define MESSAGE_BUTTON_MASK         (0x18)
#define MESSAGE_STRENGTH_MASK       (0x07)
#define MESSAGE_TYPE_OFFSET         (6)
#define MESSAGE_BUTTON_OFFSET       (3)
#define MESSAGE_STRENGTH_OFFSET     (0)

#define MESSAGE_TYPE_ATTACK         (0)
#define MESSAGE_TYPE_COUNTER_ATTACK (1)
#define MESSAGE_TYPE_RESULT         (2)
#define MESSAGE_TYPE_SCORE_ACK      (3)

struct IRMessage {
    uint8_t reserved:1;            // x:AA:BB:CCC
    uint8_t type:2;                //    |  |  \----- C = Strength (0-6)
    uint8_t button:2;              //    |  \-------- B = Button   (0-3)
    uint8_t strength:3;            //    \----------- A = Type     (0-3)
};
struct IRData {
    uint8_t header;                // 0xA2 magic header byte, ó <- looks like a little water ballon
    uint32_t id;                   // Self ID
    IRMessage msg;                 // Message [x:Type:Button:Strength]
    uint16_t score;                // Score always transmitted, used by leaderboard
    uint32_t id2;                  // Other ID
    IRMessage msg2;                // Message2 [x:Type:Button:Strength]
    bool valid;                    // Data valid
};
IRData irDataTx;
IRData irDataRx;

void rainbow(uint8_t wait);
uint32_t colorWheel(byte colorWheelPos);
void fadeOut(uint16_t wait);
int parse(decode_results *results);
bool buttonPressed();
void extendWakeTime();
void sleepAfterDelay();
int playSound(uint8_t sound, bool newSound = false);
void setDieNum(uint8_t num, uint32_t color);
int dieRoll(uint8_t finalRandomNumber, uint32_t color, bool newRoll = false);

const unsigned long SLEEP_TIMEOUT_MS = 20000;

// Default 300ms idle timeout, 200us mark timeout
// IRrecv irrecv(IR_RX_PIN);

// Optionally specify your own timeouts
const unsigned long IDLE_TIMEOUT_MS = 100;
const unsigned long MARK_TIMEOUT_US = 200;
IRrecv irrecv(IR_RX_PIN, IDLE_TIMEOUT_MS, MARK_TIMEOUT_US);

IRsend irsend(IR_TX_PIN);

extern uint8_t crc8(uint8_t data[], uint8_t len);

STARTUP(
    pinMode(D7, INPUT_PULLDOWN);
    irrecv.enableIRIn(); // Start the receiver
)

uint32_t deviceID_last4() {
    String idStr = System.deviceID();
    String idShort = idStr.substring(idStr.length()-8, idStr.length());
    // Serial.printlnf("%s %s", idStr.c_str(), idShort.c_str());
    char *ptr;
    return strtoul(idShort.c_str(), &ptr, 16);
}

bool ids_equal(uint32_t my_id, uint32_t rcv_id) {
    return (my_id == rcv_id);
}

int parse(decode_results *results) {
    // Parses the decode_results structure.
    // Call this after IRrecv::decode()

    Serial.print("Decoded BYTES: ");
    for (int i = 0; i < results->rx_len; i++) {
        Serial.printf("%02X ", results->rx_data[i]);
    }

    memset(&irDataRx, 0, sizeof(irDataRx));
    irDataRx.valid = false;

    irDataRx.header = results->rx_data[1];
    if (irDataRx.header != MAGIC_HEADER_BYTE) {
        return -1;
    }

    uint8_t type = (results->rx_data[6] & MESSAGE_TYPE_MASK) >> MESSAGE_TYPE_OFFSET;
    if (type != MESSAGE_TYPE_COUNTER_ATTACK) {
        irDataRx.msg.type = (results->rx_data[6] & MESSAGE_TYPE_MASK) >> MESSAGE_TYPE_OFFSET;
        irDataRx.msg.button = (results->rx_data[6] & MESSAGE_BUTTON_MASK) >> MESSAGE_BUTTON_OFFSET;
        irDataRx.msg.strength = (results->rx_data[6] & MESSAGE_STRENGTH_MASK) >> MESSAGE_STRENGTH_OFFSET;
    } else {
        irDataRx.msg2.type = (results->rx_data[6] & MESSAGE_TYPE_MASK) >> MESSAGE_TYPE_OFFSET;
        irDataRx.msg2.button = (results->rx_data[6] & MESSAGE_BUTTON_MASK) >> MESSAGE_BUTTON_OFFSET;
        irDataRx.msg2.strength = (results->rx_data[6] & MESSAGE_STRENGTH_MASK) >> MESSAGE_STRENGTH_OFFSET;
        irDataRx.msg.type = (results->rx_data[13] & MESSAGE_TYPE_MASK) >> MESSAGE_TYPE_OFFSET;
        irDataRx.msg.button = (results->rx_data[13] & MESSAGE_BUTTON_MASK) >> MESSAGE_BUTTON_OFFSET;
        irDataRx.msg.strength = (results->rx_data[13] & MESSAGE_STRENGTH_MASK) >> MESSAGE_STRENGTH_OFFSET;
    }

    uint32_t rcv_id = 0;
    memcpy(&rcv_id, &results->rx_data[2], 4);
    if (ids_equal(deviceID_last4(), rcv_id)) {
        Serial.println("My own ID!!");
        return -2;
    } else {
        Serial.println("Another ID!!");
    }

    if (irDataRx.msg.type == MESSAGE_TYPE_ATTACK) {
        irDataRx.id = rcv_id;
    } else if (irDataRx.msg.type == MESSAGE_TYPE_COUNTER_ATTACK) {
        irDataRx.id2 = rcv_id;
    }

    switch (irDataRx.msg.type) {
        case MESSAGE_TYPE_ATTACK: {
            setDieNum(irDataRx.msg.strength, irDataRx.msg.button+1);
            // Serial.printlnf("button: %u, strength: %u", irDataRx.msg.button+1, irDataRx.msg.strength);
            playSound(4, true);
            while (playSound(4));
            fadeOut(1000);
            break;
        }
        case MESSAGE_TYPE_COUNTER_ATTACK: {

            break;
        }
        case MESSAGE_TYPE_RESULT: {

            break;
        }
        case MESSAGE_TYPE_SCORE_ACK: {

            break;
        }
        default: {
        }
    }

    irDataRx.valid = true;
    return 0;
}

int playSound(uint8_t sound, bool newSound /* false */) {
    static system_tick_t s = millis();
    static system_tick_t i = s;
    static uint32_t timeout = 1000;
    system_tick_t now = millis();
    static bool flip = false;
    static uint16_t f = 1000;
    static uint16_t fh = 600;
    static uint16_t fl = 400;
    static uint8_t d = 10;
    static int dir = 1;
    #define SOUND_STATE_IDLE     (0)
    #define SOUND_STATE_NEW      (1)
    #define SOUND_STATE_PLAYING  (2)
    #define SOUND_STATE_FINISHED (3)
    static int state = SOUND_STATE_IDLE;

    if (newSound) {
        state = SOUND_STATE_NEW;
    }

    switch (state) {
        case SOUND_STATE_NEW: {
            s = now;
            i = s;
            dir = 1;
            f = 1000;
            fh = 250;
            fl = 200;
            d = 10;
            flip = false;
            timeout = 1000;
            if (sound == 1 || sound == 4) {
                timeout = 800;
            }
            if (sound == 3) {
                f = 800;
                timeout = 2000;
            }
            state = SOUND_STATE_PLAYING;
            break;
        }
        case SOUND_STATE_PLAYING: {
            if (now - s < timeout) {
                switch (sound) {
                case 1: {
                    // NOISE
                    if (now - i > 1) {
                        i = now;
                        int r = random(50, 300);
                        analogWrite(SPEAKER_PIN, 128, r);
                    }
                    break;
                }
                case 2: {
                    // SPACE SOUND
                    if (now - i > 20) {
                        i = now;
                        if (flip) {
                            analogWrite(SPEAKER_PIN, 128, f);
                            f -= 5;
                        } else {
                            analogWrite(SPEAKER_PIN, 0, f);
                        }
                        flip = !flip;
                    }
                    break;
                }
                case 3: {
                    // THROW SOUND
                    if (now - i > 10) {
                        i = now;
                        analogWrite(SPEAKER_PIN, 128, f);
                        if (dir == 1) {
                            if (f < 1000) {
                                f += 15;
                            } else {
                                dir = 0;
                            }
                        } else {
                            if (f > 700) {
                                f -= 4;
                            } else {
                                state = SOUND_STATE_FINISHED;
                            }
                        }
                    }
                    break;
                }
                case 4: {
                    // SPLASH SOUND
                    if (now - i > 5) {
                        i = now;
                        int f = random(fl, fh);
                        // Serial.println(f);
                        analogWrite(SPEAKER_PIN, d, f);
                        if (dir == 1) {
                            if (d < 128) {
                                d += 10;
                            } else {
                                dir = 0;
                            }
                        } else {
                            if (d > 0) {
                                d -= 1;
                            } else {
                                state = SOUND_STATE_FINISHED;
                            }
                        }
                        if (fh < 350) {
                            fh+=2;
                        }
                        if (fl > 50) {
                            fl-=2;
                        }
                    }
                    break;
                }
                // case 5: {
                //     // MAGNETIC SOUND
                //     if (now - i > 3) {
                //         i = now;
                //                                  // fh starts at 300
                //         analogWrite(SPEAKER_PIN, d, fh);
                //         if (dir == 1) {
                //             if (d < 150) {
                //                 d += 5;
                //             } else {
                //                 dir = 0;
                //             }
                //         } else {
                //             if (d > 0) {
                //                 d -= 1;
                //             } else {
                //                 state = SOUND_STATE_FINISHED;
                //             }
                //         }
                //         if (fh > 200) {
                //             fh-=2;
                //         }
                //     }
                //     break;
                // }
                case 0:
                default: {
                }
                } // switch (sound)
            } else {
                state = SOUND_STATE_FINISHED;
            }
            break;
        }
        case SOUND_STATE_FINISHED: {
            analogWrite(SPEAKER_PIN, 0, 1000);
            state = SOUND_STATE_IDLE;
            break;
        }
        case SOUND_STATE_IDLE: {
            break;
        }
    } // switch (state)

    if (state == SOUND_STATE_IDLE) {
        return 0;
    } else {
        return 1;
    }
}

void setDieNum(uint8_t num, uint32_t color) {
                                         // OFF, 1, 2, 3, 4, 5, 6, ROW 1, ROW 2, ROW 3
    static const uint8_t numToPixel[11] = {0, 0b00001000, 0b00010100, 0b01001001, 0b01010101, 0b01011101, 0b01110111, 0b00010001, 0b00101010, 0b01000100};
    static const uint32_t colors[4] = {0x00640064, 0x00009050, 0x00646400, 0x000000FF}; // magenta, cyan, yellow, blue

    for (int i = 0; i < 7; i++) {
        strip.setPixelColor(i, 0);
    }

    if (num == 0) {
        strip.show();
        return;
    }

    for (uint8_t x = 0, y = 1; y <= 0x40; x++, y <<= 1) {
        if (numToPixel[num] & y) {
            strip.setPixelColor(x, colors[color-1]);
        }
    }
    strip.show();
}

int dieRoll(uint8_t finalRandomNumber, uint32_t color, bool newRoll /* false */) {
    static system_tick_t lastUpdate = millis();
    static system_tick_t startRoll = 0;
    static const uint8_t dieVals[6] = {1, 3, 2, 5, 6, 4}; // Ordered to show the most change in LEDs when rolling
    static uint8_t dieIdx = 0;
    static uint8_t dieRow = 0; // 0:RGB off, 1:RGB on-color, 2:RGB off/ROW 1, 3:RGB off/ROW 2, 4:RGB off/ROW 3
    uint32_t now = millis();
    static uint8_t sound = 0;
    #define DIE_ROLL_INTERVAL_MS    (50)
    #define DIE_ROLL_TIMEOUT_MS     (1000)
    #define DIE_ROLL_STATE_IDLE     (0)
    #define DIE_ROLL_STATE_NEW      (1)
    #define DIE_ROLL_STATE_THROW    (2)
    #define DIE_ROLL_STATE_ROLLING  (3)
    #define DIE_ROLL_STATE_FINISHED (4)
    static int state = DIE_ROLL_STATE_IDLE;

    if (newRoll) {
        state = DIE_ROLL_STATE_NEW;
    }

    switch (state) {
        case DIE_ROLL_STATE_NEW: {
            lastUpdate = startRoll - DIE_ROLL_INTERVAL_MS;
            dieIdx = 0;
            dieRow = 0;
            sound = color; // same index range
            playSound(sound, true); // reset sound
            state = DIE_ROLL_STATE_THROW;
            break;
        }
        case DIE_ROLL_STATE_THROW: {
            // This state visualizes throwing a Water Balloon away from the badge
            if (now - lastUpdate >= DIE_ROLL_INTERVAL_MS) {
                lastUpdate = now;
                if (dieRow == 1) {
                    if (color == 1) {
                        RGB.color(200, 0, 200);
                    } else if (color == 2) {
                        RGB.color(0, 200, 200);
                    } else if (color == 3) {
                        RGB.color(200, 200, 0);
                    } else if (color == 4) {
                        RGB.color(0, 0, 200);
                    }
                } else {
                    RGB.color(0, 0, 0);
                }
                if (dieRow >= 2 && dieRow <= 4) {
                    setDieNum(dieRow + 5, color);
                } else {
                    setDieNum(0, color);
                }
                dieRow++;
                // Give some OFF delay before rolling
                if (dieRow > 8) {
                    startRoll = now;
                    state = DIE_ROLL_STATE_ROLLING;
                }
            }
            playSound(sound);
            break;
        }
        case DIE_ROLL_STATE_ROLLING: {
            if (now - startRoll >= DIE_ROLL_TIMEOUT_MS) {
                state = DIE_ROLL_STATE_FINISHED;
                // Serial.printlnf("TIMEOUT: %lu", now - startRoll);
                break;
            }
            if (now - lastUpdate >= DIE_ROLL_INTERVAL_MS * 2) {
                // Serial.printlnf("UPDATE: %lu", now - lastUpdate);
                lastUpdate = now;
                setDieNum(dieVals[dieIdx], color);
                dieIdx++;
                if (dieIdx > 5) {
                    dieIdx = 0;
                }
            }
            if (!playSound(sound)) {
                state = DIE_ROLL_STATE_FINISHED;
            }
            break;
        }
        case DIE_ROLL_STATE_FINISHED: {
            setDieNum(finalRandomNumber, color);
            // Serial.printlnf("END RAND: %d", finalRandomNumber);
            // RGB.color(0, 150, 150);
            state = DIE_ROLL_STATE_IDLE;
            break;
        }
        case DIE_ROLL_STATE_IDLE:
        default: {
        }
    }

    // system_tick_t start = millis();
    // while (millis() - start < wait) {
    //     switch(colorPick) {
    //         case 1: {
    //             // NOISE
    //             int r = random(20, 400);
    //             analogWrite(SPEAKER_PIN, 128, r);
    //             delay(10);
    //             break;
    //         }
    //         case 2: {
    //             // SPACE SOUND
    //             analogWrite(SPEAKER_PIN, 128, f);
    //             f -= 10;
    //             delay(20);
    //             analogWrite(SPEAKER_PIN, 0, f);
    //             delay(20);
    //             break;
    //         }
    //         case 3: {
    //             // UFO SOUND
    //             analogWrite(SPEAKER_PIN, 128, f);
    //             if (dir == 1) {
    //                 if (f < 1200) {
    //                     f += 10;
    //                 } else {
    //                     dir = 0;
    //                 }
    //             } else {
    //                 if (f > 800) {
    //                     f -= 10;
    //                 } else {
    //                     dir = 1;
    //                 }
    //             }
    //             delay(10);
    //             break;
    //         }
    //         case 0:
    //         default: {

    //         }
    //     }
    // }
    // analogWrite(SPEAKER_PIN, 0, 1000);

    if (state == DIE_ROLL_STATE_IDLE) {
        return 0;
    } else {
        return 1;
    }
}

void extendWakeTime() {
    lastSleep = millis();
}

void sleepAfterDelay() {
    if (millis() - lastSleep >= SLEEP_TIMEOUT_MS) {
        Serial.println("Going to sleep");
        digitalWrite(PIXEL_ENABLE_PIN, LOW);
        delay(500);

        SystemSleepConfiguration config;
        // config.mode(SystemSleepMode::HIBERNATE)
            config.mode(SystemSleepMode::ULTRA_LOW_POWER)
            .gpio(IR_RX_PIN, FALLING)
            .gpio(BUTTON_1_PIN, FALLING)
            .gpio(BUTTON_2_PIN, FALLING)
            .gpio(BUTTON_3_PIN, FALLING)
            .gpio(BUTTON_4_PIN, FALLING)
            .duration(30s);
        SystemSleepResult sleepResult = System.sleep(config);

        wakeupReason = (int)sleepResult.wakeupReason();
        // Serial.printlnf("Wake reason:%d, Wakeup pin:%d\n", wakeupReason, wakeupPin);

        if (wakeupReason == (int)SystemSleepWakeupReason::BY_GPIO) {
            wakeupPin = (int)sleepResult.wakeupPin();
        } else {
            wakeupPin = PIN_INVALID;
        }

        digitalWrite(PIXEL_ENABLE_PIN, HIGH);

        extendWakeTime();
    }
}

void rainbow(uint8_t wait) {
    uint16_t i, j;

    for (j = 0; j < 256; j++) {
        for (i = 0; i < strip.numPixels(); i++) {
            strip.setPixelColor(i, colorWheel((i + j) & 255));
        }
        strip.show();
        delay(wait);
    }
}

// Input a value 0 to 255 to get a color value.
// The colours are a transition r - g - b - back to r.
uint32_t colorWheel(byte colorWheelPos) {
    if (colorWheelPos < 85) {
        return strip.Color(colorWheelPos * 3, 255 - colorWheelPos * 3, 0);
    } else if (colorWheelPos < 170) {
        colorWheelPos -= 85;
        return strip.Color(255 - colorWheelPos * 3, 0, colorWheelPos * 3);
    } else {
        colorWheelPos -= 170;
        return strip.Color(0, colorWheelPos * 3, 255 - colorWheelPos * 3);
    }
}

// Fade out pixels to zero
void fadeOut(uint16_t wait) {
    for (int f = 0; f < 20; f++) {
        for (int i = 0; i < strip.numPixels(); i++) {
            uint32_t c = strip.getPixelColor(i);
            uint8_t r = (c >> 16) & 0xff;
            uint8_t g = (c >> 8) & 0xff;
            uint8_t b = c & 0xff;
            r -= r/4;
            g -= g/4;
            b -= b/4;
            strip.setPixelColor(i, r, g, b);
        }
        strip.show();
        delay(wait/20);
    }
}

// Set all pixels in the strip to a solid color, then wait (ms)
void colorAll(uint32_t c, uint16_t wait) {
    uint16_t i;

    uint16_t f = 1000;
    int dir = 1;

    for (i = 0; i < strip.numPixels(); i++) {
        strip.setPixelColor(i, c);
    }
    strip.show();

    system_tick_t start = millis();
    while (millis() - start < wait) {
        switch(colorPick) {
            case 1: {
                // NOISE
                int r = random(20, 400);
                analogWrite(SPEAKER_PIN, 128, r);
                delay(10);
                break;
            }
            case 2: {
                // SPACE SOUND
                analogWrite(SPEAKER_PIN, 128, f);
                f -= 10;
                delay(20);
                analogWrite(SPEAKER_PIN, 0, f);
                delay(20);
                break;
            }
            case 3: {
                // UFO SOUND
                analogWrite(SPEAKER_PIN, 128, f);
                if (dir == 1) {
                    if (f < 1200) {
                        f += 10;
                    } else {
                        dir = 0;
                    }
                } else {
                    if (f > 800) {
                        f -= 10;
                    } else {
                        dir = 1;
                    }
                }
                delay(10);
                break;
            }
            case 0:
            default: {

            }
        }
    }
    analogWrite(SPEAKER_PIN, 0, 1000);
}

bool buttonPressed() {
    bool pressed = false;
    if (wakeupPin != PIN_INVALID) {
        if (wakeupPin == BUTTON_1_PIN) {
            colorPick = 1;
        } else if (wakeupPin == BUTTON_2_PIN) {
            colorPick = 2;
        } else if (wakeupPin == BUTTON_3_PIN) {
            colorPick = 3;
        } else if (wakeupPin == BUTTON_4_PIN) {
            colorPick = 4;
        } else if (wakeupPin == IR_RX_PIN) {
            colorPick = 5;
        }
        // pressed = true;
        wakeupPin = PIN_INVALID;
    } else if (digitalRead(BUTTON_1_PIN) == LOW) {
        colorPick = 1;
        pressed = true;
    } else if (digitalRead(BUTTON_2_PIN) == LOW) {
        colorPick = 2;
        pressed = true;
    } else if (digitalRead(BUTTON_3_PIN) == LOW) {
        colorPick = 3;
        pressed = true;
    } else if (digitalRead(BUTTON_4_PIN) == LOW) {
        colorPick = 4;
        pressed = true;
    }
    // if (digitalRead(BUTTON_1_PIN) == LOW || digitalRead(BUTTON_2_PIN) == LOW || digitalRead(BUTTON_3_PIN) == LOW || digitalRead(BUTTON_4_PIN) == LOW) {
    //     pressed = true;
    // }
    return pressed;
}

void setup() {
    // irrecv.enableIRIn(); // Start the receiver
    pinMode(PIXEL_ENABLE_PIN, OUTPUT);
    digitalWrite(PIXEL_ENABLE_PIN, HIGH);
    strip.begin();
    strip.setBrightness(50);
    strip.show(); // Initialize all pixels to 'off'

    pinMode(BUTTON_1_PIN, INPUT_PULLUP);
    pinMode(BUTTON_2_PIN, INPUT_PULLUP);
    pinMode(BUTTON_3_PIN, INPUT_PULLUP);
    pinMode(BUTTON_4_PIN, INPUT_PULLUP);

    Serial.begin();

    pinMode(SPEAKER_PIN, OUTPUT);
    analogWrite(SPEAKER_PIN, 0, 1000);

    RGB.control(true);
    RGB.color(0,150,150);

    BLE.off();
    WiFi.off();
    WiFi.clearCredentials();

    // if (!sht31.begin(0x44)) {
    //     Serial.println("Couldn't find SHT31");
    // }

    // for (int x = 0; x < 10; x++) {
    //     float t = sht31.readTemperature();
    //     float h = sht31.readHumidity();
    //     float tF = ((t * 9) / 5) + 32;
    //     if (!isnan(t)) {
    //         //Temperature in C
    //         Serial.printlnf("Temp = %0.2f°C", t);
    //         //Temperature in F
    //         Serial.printlnf("Temp = %0.2f°F", tF);
    //     } else {
    //         Serial.println("Failed to read temperature");
    //     }
    //     if (!isnan(h)) {
    //         Serial.printlnf("Hum. = %0.2f%%", h);
    //     } else {
    //         Serial.println("Failed to read humidity");
    //     }
    //     delay(1000);
    // }
}

void loop() {
    if (irrecv.decode(&irResults)) {
        if (irResults.decode_type == BYTES) {
            if (parse(&irResults) == 0) {
                // new message to process
            }
        }
        irrecv.resume(); // Receive the next value
    }

    // sleepAfterDelay();

    if (irrecv.decode(&irResults)) {
        if (irResults.decode_type == BYTES) {
            if (parse(&irResults) == 0) {
                // new message to process
            }
        }
        irrecv.resume(); // Receive the next value
    }

    buttonPressed();
    if (colorPick > 0) {
        uint8_t randomNumber = random(6)+1;
        // static uint8_t randomNumber = 0;
        // randomNumber++;
        // if (randomNumber > 6) {
        //     randomNumber = 1;
        // }
        dieRoll(randomNumber, colorPick, true); // force new roll
        Serial.printlnf("START RAND: %d", randomNumber);
        uint32_t s = millis();
        int rolling = 1;
        while (millis() - s < 2000 && rolling) {

        switch (colorPick) {
            case 1: {
                rolling = dieRoll(randomNumber, colorPick);
                break;
            }
            case 2: {
                // colorAll(strip.Color(0, 255, 0), 1000); // GREEN
                // colorAll(strip.Color(100, 100, 0), 1000); // YELLOW
                rolling = dieRoll(randomNumber, colorPick);
                break;
            }
            case 3: {
                // colorAll(strip.Color(0, 100, 100), 1000); // CYAN
                rolling = dieRoll(randomNumber, colorPick);
                break;
            }
            case 4: {
                // colorAll(strip.Color(0, 0, 255), 1000); // BLUE
                rolling = dieRoll(randomNumber, colorPick);
                break;
            }
            default:
            case 5: {
                rainbow(4);
                // colorAll(strip.Color(0, 0, 255), 1000); // BLUE
                break;
            }
        }

        } // while

        //HEADER:P1_ID:[MSG_TYP:MSG_BTN:MSG_STR]:P1_SCORE:CRC
        uint32_t id32bit = deviceID_last4();

        uint8_t dataBuf[8] = {};//{0xA2, 0x12, 0x34, 0x56, 0x78, 0b00110111, 9, 9};
        dataBuf[0] = MAGIC_HEADER_BYTE; // magic header byte, ó - looks like a little waterballon
        memcpy(&dataBuf[1], &id32bit, 4);
        dataBuf[5] = (MESSAGE_TYPE_ATTACK << MESSAGE_TYPE_OFFSET) + ((colorPick-1) << MESSAGE_BUTTON_OFFSET) + (randomNumber);
        dataBuf[6] = 1200 >> 8;
        dataBuf[7] = 1200 & 0xff;

        irrecv.disableIRIn();
        irsend.sendBytes(dataBuf, 8);
        irrecv.enableIRIn();
        // delay(100);

        // colorAll(strip.Color(0, 0, 0), 0); // OFF
        fadeOut(1000);
        while (buttonPressed());

        colorPick = 0;
        wakeupReason = 0;

        analogWrite(SPEAKER_PIN, 0, 1000);

        extendWakeTime();
    }
}
