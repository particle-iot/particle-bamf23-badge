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

#define ENABLE_ON_BOARD_SHT31 (0)
#define ENABLE_QWIIC_SENSOR_DEMO (0)

#if ENABLE_ON_BOARD_SHT31
#include "adafruit-sht31.h"
Adafruit_SHT31 sht31 = Adafruit_SHT31();
#endif // ENABLE_ON_BOARD_SHT31

#if ENABLE_QWIIC_SENSOR_DEMO
#include "SparkFun_VCNL4040.h"
VCNL4040 proximitySensor;
long startingProxValue = 0;
long deltaNeeded = 0;
boolean nothingThere = false;
#endif // ENABLE_QWIIC_SENSOR_DEMO

SYSTEM_MODE(SEMI_AUTOMATIC);
// SYSTEM_THREAD(ENABLED);

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

// ATTACK (P1): HEADER:P1_ID:[MSG_TYP:MSG_BTN:MSG_STR]:P1_SCORE:CRC
//              1:4:1:2 (8)
//
// COUNTER(P2): HEADER:P2_ID:[MSG_TYP:MSG_BTN:MSG_STR]:P2_SCORE:P1_ID:[MSG_TYP:MSG_BTN:MSG_STR]:CRC
//              1:4:1:2:4:1 (13)
//
// RESULT (P1): HEADER:P1_ID:[MSG_TYP:MSG_BTN:MSG_STR]:P1_P2_ID:CRC
//              1:4:1:4 (10)
//
// SCORE_ACK(P2): HEADER:P1_ID:[MSG_TYP:MSG_BTN:MSG_STR]:P1_SCORE:CRC
//                1:4:1:2 (8)
//
#define MAGIC_HEADER_BYTE                   (0xA2)
#define MESSAGE_TYPE_MASK                   (0xE0)
#define MESSAGE_BUTTON_MASK                 (0x18)
#define MESSAGE_STRENGTH_MASK               (0x07)
#define MESSAGE_TYPE_OFFSET                 (5)
#define MESSAGE_BUTTON_OFFSET               (3)
#define MESSAGE_STRENGTH_OFFSET             (0)

#define MESSAGE_TYPE_ATTACK                 (0)
#define MESSAGE_TYPE_ATTACK_ACK             (1)
#define MESSAGE_TYPE_COUNTER_ATTACK         (2)
#define MESSAGE_TYPE_COUNTER_ATTACK_ACK     (3)
#define MESSAGE_TYPE_RESULT                 (4)
#define MESSAGE_TYPE_RESULT_ACK             (5)
#define MESSAGE_TYPE_SCORE_ACK              (6)

#define MESSAGE_TYPE_ATTACK_LEN             (7)
#define MESSAGE_TYPE_ATTACK_ACK_LEN         (7)
#define MESSAGE_TYPE_COUNTER_ATTACK_LEN     (12)
#define MESSAGE_TYPE_COUNTER_ATTACK_ACK_LEN (12)
#define MESSAGE_TYPE_RESULT_LEN             (9)
#define MESSAGE_TYPE_RESULT_ACK_LEN         (9)
#define MESSAGE_TYPE_SCORE_ACK_LEN          (7)

#define SOUND_STATE_IDLE                    (0)
#define SOUND_STATE_NEW                     (1)
#define SOUND_STATE_PLAYING                 (2)
#define SOUND_STATE_FINISHED                (3)

#define DIE_ROLL_INTERVAL_MS                (50)
#define DIE_ROLL_TIMEOUT_MS                 (1000)
#define DIE_ROLL_TIMEOUT_MAX_MS             (2000)
#define DIE_ROLL_STATE_IDLE                 (0)
#define DIE_ROLL_STATE_NEW                  (1)
#define DIE_ROLL_STATE_THROW                (2)
#define DIE_ROLL_STATE_ROLLING              (3)
#define DIE_ROLL_STATE_FINISHED             (4)

#define DIE_COLOR_MAGENTA                   (1)
#define DIE_COLOR_CYAN                      (2)
#define DIE_COLOR_YELLOW                    (3)
#define DIE_COLOR_BLUE                      (4)
#define DIE_COLOR_RED                       (5)
#define DIE_COLOR_GREEN                     (6)
#define DIE_COLOR_WHITE                     (7)

#define GAME_RESULT_WIN                     (0)
#define GAME_RESULT_LOSE                    (1)
#define GAME_RESULT_DRAW                    (2)
#define GAME_RESULT_INVALID                 (3)

#define BADGE_STATE_IDLE                    (0)
#define BADGE_STATE_WAKEUP                  (1)
#define BADGE_STATE_SLEEP                   (2)
#define BADGE_STATE_MESSAGE_AVAILABLE       (3)
#define BADGE_STATE_DIE_ROLL_INIT           (4)
#define BADGE_STATE_DIE_ROLL                (5)
#define BADGE_STATE_SPLASH                  (6)
#define BADGE_STATE_DISPLAY_RESULT          (7)
int badgeState = BADGE_STATE_IDLE;

#define GAMEPLAY_STATE_IDLE                 (0)
#define GAMEPLAY_STATE_ATTACK               (1)
#define GAMEPLAY_STATE_ATTACK_ACK           (2)
#define GAMEPLAY_STATE_COUNTER_ATTACK       (3)
#define GAMEPLAY_STATE_COUNTER_ATTACK_ACK   (4)
#define GAMEPLAY_STATE_RESULT               (5)
#define GAMEPLAY_STATE_RESULT_ACK           (6)
#define GAMEPLAY_STATE_SCORE_ACK            (7)
#define GAMEPLAY_STATE_RESULT_DISPLAY       (8)
int gameStateP1 = GAMEPLAY_STATE_IDLE; // our state machine
int gameStateP2 = GAMEPLAY_STATE_IDLE; // keep track of their state machine
#define GAME_STATE_TIMEOUT_MS               (8000)

struct IRMessage {
                                   // MSB[AAA:BB:CCC]LSB
    uint8_t strength:3;            //      |  |  \----- C = Strength (0-6)
    uint8_t button:2;              //      |  \-------- B = Button   (0-3)
    uint8_t type:3;                //      \----------- A = Type     (0-6)
};
struct IRData {
    uint8_t header;                // 0xA2 magic header byte, ó <- looks like a little water ballon
    uint32_t id;                   // Self ID
    IRMessage msg;                 // Message [x:Type:Button:Strength]
    uint16_t score;                // Score always transmitted, used by leaderboard
    uint32_t id2;                  // Other ID
    IRMessage msg2;                // Message2 [x:Type:Button:Strength]
    uint8_t valid;                 // Data valid
};
IRData irDataTx;
IRData irDataRx;
IRData irDataRxCurrent; // saved copy of Rx message to compare through the state machine

decode_results irResults;
system_tick_t lastSleep = 0;
int wakeupReason = 0;
int wakeupPin = PIN_INVALID;
int colorPick = 0;
bool muteSound = false;

uint16_t player1score = 0;
uint16_t player2score = 0;

uint32_t player2id = 0;
IRMessage player1msg = {};
IRMessage player2msg = {};

uint32_t winner_id = 0;
uint32_t received_winner_id = 0;
#define GAME_IS_A_DRAW_ID (0x13371337)
int gameResult = GAME_RESULT_INVALID;

uint8_t randomNumber = 1;
uint32_t startDieRoll = 0;
uint32_t startGamestateTimer = 0;
int rolling = 0;
int retransmits = 0;
uint32_t startRetransmit = 0;
uint32_t retransmitDelay = 500;
#define DATA_BUF_LEN (13) // HEADER,                P1_ID, [MSG_TYP, MSG_BTN, MSG_STR], P1_SCORE, CRC
uint8_t dataBuf[DATA_BUF_LEN] = {}; // {0xA2,   0x12,0x34,0x56,0x78,                  0b00110111,     1200, 9};

void rainbow(uint8_t wait);
uint32_t colorWheel(byte colorWheelPos);
void fadeOut(uint16_t wait);
int parse(decode_results *results);
int buttonPressed();
void extendWakeTime();
void sleepAfterDelay();
int playSound(uint8_t sound, int soundState);
void setDieNum(uint8_t num, uint32_t color);
int dieRoll(uint8_t finalRandomNumber, uint32_t color, bool newRoll = false);
void createMessage(uint8_t* buf, int msgType, int button, int strength, uint32_t player2id = 0, void* player2msg = nullptr);

const unsigned long SLEEP_TIMEOUT_MS = 60000;

// Default 300ms idle timeout, 200us mark timeout
// IRrecv irrecv(IR_RX_PIN);

// Optionally specify your own timeouts
const unsigned long IDLE_TIMEOUT_MS = 100;
const unsigned long MARK_TIMEOUT_US = 200;
IRrecv irrecv(IR_RX_PIN, IDLE_TIMEOUT_MS, MARK_TIMEOUT_US);

IRsend irsend(IR_TX_PIN);

extern uint8_t crc8(uint8_t data[], uint8_t len);

#define EEPROM_VERSION             (1337)
#define EEPROM_ADDRESS             (10)
#define EEPROM_PLAYER_ID_OFFSET    (0)
#define EEPROM_PLAYER_PLAYS_OFFSET (1)
#define EEPROM_PLAYER_PLAYS_MAX    (10)
struct EEdata {
    uint16_t version;  // detect if EE uninitialized
    uint16_t score;
    uint8_t idWrite; // circular pointer to next place to write in 10 id buffer
    uint32_t ids[10][2]; // 10 ids, non-tie plays (10 max)
} eeData;

int playerIdFind(uint32_t player2) {
    for (int x = 0; x < 10; x++) {
        if (eeData.ids[x][EEPROM_PLAYER_ID_OFFSET] == player2) {
            return x;
        }
    }
    return -1;
}

void initEEPROM() {
    memset(&eeData, 0, sizeof(eeData));
    eeData.version = EEPROM_VERSION;
    EEPROM.put(EEPROM_ADDRESS, eeData);
    delay(100);
    EEPROM.get(EEPROM_ADDRESS, eeData);
    if (eeData.version != EEPROM_VERSION) {
        Serial.printlnf("EEPROM initialization failed! %d", eeData.version);
    } else {
        Serial.printlnf("EEPROM initialized! %d", eeData.version);
    }
}

uint8_t findPlayerSaveSlot(uint32_t player2) {
    int player2lookup = playerIdFind(player2);
    if (player2lookup != -1) {
        return player2lookup;
    } else {
        // increment circular pointer and zero out player save slot
        if (++eeData.idWrite >= 10) {
            eeData.idWrite = 0;
        }
        eeData.ids[eeData.idWrite][EEPROM_PLAYER_ID_OFFSET] = player2;
        eeData.ids[eeData.idWrite][EEPROM_PLAYER_PLAYS_OFFSET] = 0;
        // Serial.println("ids wrapped");
    }

    return eeData.idWrite;
}

void readEEPROM() {
    EEPROM.get(EEPROM_ADDRESS, eeData);
    if (eeData.version != EEPROM_VERSION) {
        // EEPROM was wrong version, initialize!
        initEEPROM();
    }
}

void writeEEPROM() {
    if (eeData.version != EEPROM_VERSION) {
        // EEPROM was wrong version, initialize!
        initEEPROM();
    }
    EEPROM.put(EEPROM_ADDRESS, eeData);
}

void loadScore() {
    readEEPROM();
    player1score = eeData.score;
    Serial.printlnf("< PLAYER SCORE: %d", player1score);
}

void saveScore() {
    eeData.score = player1score;
    writeEEPROM();
    Serial.printlnf("> PLAYER SCORE: %d", player1score);
}

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

// #define PAR_LEN_OFF    (0)
// #define PAR_HDR_OFF    (1)
// #define PAR_ID1_OFF    (2)
//                      //(3)
//                      //(4)
//                      //(5)
// #define PAR_MSG1_OFF   (6)
// #define PAR_WIN_ID_OFF (7)
// #define PAR_SCR1_OFF   (7)
// #define PAR_SCR2_OFF   (8)
// #define PAR_ID2_OFF    (9)
//                      //(10)
//                      //(11)
//                      //(12)
// #define PAR_MSG2_OFF   (13)
// #define PAR_CRC_OFF    (14)

#define PAR_LEN_OFF    (0)
// #define PAR_HDR_OFF    (1)
#define PAR_ID1_OFF    (1)
                     //(2)
                     //(3)
                     //(4)
#define PAR_MSG1_OFF   (5)
#define PAR_WIN_ID_OFF (6)
#define PAR_SCR1_OFF   (6)
#define PAR_SCR2_OFF   (7)
#define PAR_ID2_OFF    (8)
                     //(9)
                     //(10)
                     //(11)
#define PAR_MSG2_OFF   (12)
#define PAR_CRC_OFF    (13)
int parse(decode_results *results) {
    // Parses the decode_results structure.
    // Call this after IRrecv::decode()

    // Serial.print("Decoded BYTES: ");
    // for (int i = 0; i < results->rx_len; i++) {
    //     Serial.printf("%02X ", results->rx_data[i]);
    // }

    memset(&irDataRx, 0, sizeof(irDataRx));
    irDataRx.valid = 0;

    // irDataRx.header = results->rx_data[MSG_HDR_OFF];
    // if (irDataRx.header != MAGIC_HEADER_BYTE) {
    //     return -1;
    // }

    uint8_t type = (results->rx_data[PAR_MSG1_OFF] & MESSAGE_TYPE_MASK) >> MESSAGE_TYPE_OFFSET;
    if (type != MESSAGE_TYPE_COUNTER_ATTACK && type != MESSAGE_TYPE_COUNTER_ATTACK_ACK) {
        irDataRx.msg.type = (results->rx_data[PAR_MSG1_OFF] & MESSAGE_TYPE_MASK) >> MESSAGE_TYPE_OFFSET;
        irDataRx.msg.button = (results->rx_data[PAR_MSG1_OFF] & MESSAGE_BUTTON_MASK) >> MESSAGE_BUTTON_OFFSET;
        irDataRx.msg.strength = (results->rx_data[PAR_MSG1_OFF] & MESSAGE_STRENGTH_MASK) >> MESSAGE_STRENGTH_OFFSET;
        // Serial.printf(" T1:%d B1:%d S1:%d ", irDataRx.msg.type, irDataRx.msg.button, irDataRx.msg.strength);
    } else {
        irDataRx.msg.type = (results->rx_data[PAR_MSG1_OFF] & MESSAGE_TYPE_MASK) >> MESSAGE_TYPE_OFFSET;
        irDataRx.msg.button = (results->rx_data[PAR_MSG1_OFF] & MESSAGE_BUTTON_MASK) >> MESSAGE_BUTTON_OFFSET;
        irDataRx.msg.strength = (results->rx_data[PAR_MSG1_OFF] & MESSAGE_STRENGTH_MASK) >> MESSAGE_STRENGTH_OFFSET;
        irDataRx.msg2.type = (results->rx_data[PAR_MSG2_OFF] & MESSAGE_TYPE_MASK) >> MESSAGE_TYPE_OFFSET;
        irDataRx.msg2.button = (results->rx_data[PAR_MSG2_OFF] & MESSAGE_BUTTON_MASK) >> MESSAGE_BUTTON_OFFSET;
        irDataRx.msg2.strength = (results->rx_data[PAR_MSG2_OFF] & MESSAGE_STRENGTH_MASK) >> MESSAGE_STRENGTH_OFFSET;
        // Serial.printf(" T1:%d B1:%d S1:%d T2:%d B2:%d S2:%d ", irDataRx.msg.type, irDataRx.msg.button, irDataRx.msg.strength,
                                                              // irDataRx.msg2.type, irDataRx.msg2.button, irDataRx.msg2.strength);
    }

    player2score = (results->rx_data[PAR_SCR1_OFF] << 8) + results->rx_data[PAR_SCR2_OFF];
    // Serial.printf("player2score:%d", player2score);

    uint32_t rcv_id = 0;
    uint32_t win_id = 0;
    uint32_t acked_id = 0;
    memcpy(&rcv_id, &results->rx_data[PAR_ID1_OFF], 4);
    if (ids_equal(deviceID_last4(), rcv_id)) {
        // Serial.println("My own ID!!");
        return -2;
    } else {
        // Serial.println("Another ID!!");
    }
    irDataRx.id = rcv_id;

    if (irDataRx.msg.type == MESSAGE_TYPE_RESULT || irDataRx.msg.type == MESSAGE_TYPE_RESULT_ACK) {
        memcpy(&win_id, &results->rx_data[PAR_WIN_ID_OFF], 4);
        // Serial.printlnf("(US) player1id:%08lX %d [win_id:%08lX] player2id:%08lX %d (THEM)", deviceID_last4(), player1msg.strength, win_id, player2id, player2msg.strength);
        if (ids_equal(deviceID_last4(), win_id) ||
                ids_equal(player2id, win_id) ||
                ids_equal(GAME_IS_A_DRAW_ID, win_id)) {
            irDataRx.id2 = win_id;
            received_winner_id = win_id;
        } else {
            // Serial.println("Unknown winner ID!!");
            irDataRx.id2 = 0;
            received_winner_id = 0xdeadbeef;
            return -3;
        }
    } else if (irDataRx.msg.type == MESSAGE_TYPE_COUNTER_ATTACK) {
        memcpy(&acked_id, &results->rx_data[PAR_ID2_OFF], 4);
        if (ids_equal(deviceID_last4(), acked_id)) {
            // Serial.println("My own ID ACK'd");
        } else {
            // Serial.println("Another ACK'd ID!!");
            return -4;
        }
        irDataRx.id2 = acked_id;
    }

    irDataRx.valid = 1;
    return 0;
}

int playSound(uint8_t sound, int soundState) {
    if (muteSound) {
        return 0;
    }

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
    static int state = SOUND_STATE_IDLE;

    if (soundState == SOUND_STATE_NEW) {
        state = SOUND_STATE_NEW;
    } else if (soundState == SOUND_STATE_FINISHED) {
        state = SOUND_STATE_FINISHED;
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
            timeout = 1000; // THROW 3
            if (sound == 1) { // THROW 1
                f = 800;
            } else if (sound == 2) { // THROW 2
                f = 1000;
                timeout = 2000;
            } else if (sound == 4) { // THROW 4
                f = 800;
                timeout = 2000;
            } else if (sound == 5) { // SPLASH
                timeout = 800;
            } else if (sound == 7) { // SPLASH
                timeout = 500;
            } else if (sound == 8) { // SPLASH
                timeout = 1200;
            }
            state = SOUND_STATE_PLAYING;
            break;
        }
        case SOUND_STATE_PLAYING: {
            if (now - s < timeout) {
                switch (sound) {
                case 1: {
                    // THROW SOUND 1
                    if (now - i > 30) {
                        i = now;
                        if (flip) {
                            analogWrite(SPEAKER_PIN, 128, f + 100);
                            f -= 7;
                        }
                        else {
                            analogWrite(SPEAKER_PIN, 128, f - 100);
                        }
                        flip = !flip;
                    }
                    break;
                }
                case 2: {
                    // THROW SOUND 2
                    if (now - i > 15) {
                        i = now;
                        analogWrite(SPEAKER_PIN, 128, f);
                        if (dir == 1) {
                            if (f < 1200) {
                                f += 15;
                            } else {
                                dir = 0;
                            }
                        } else {
                            if (f > 700) {
                                f -= 5;
                            } else {
                                state = SOUND_STATE_FINISHED;
                            }
                        }
                    }
                    break;
                }
                case 3: {
                    // THROW SOUND 3
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
                case 4: {
                    // THROW SOUND 4
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
                case 5: {
                    // MAGNETIC SOUND
                    if (now - i > 3) {
                        i = now;
                                                 // fh starts at 300
                        analogWrite(SPEAKER_PIN, d, fh);
                        if (dir == 1) {
                            if (d < 150) {
                                d += 5;
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
                        if (fh > 200) {
                            fh-=2;
                        }
                    }
                    break;
                }
                case 6: {
                    // SIMPLE NOISE 1
                    if (now - i > 1) {
                        i = now;
                        int r = random(100, 400);
                        analogWrite(SPEAKER_PIN, 128, r);
                    }
                    break;
                }
                case 7: {
                    // SIMPLE NOISE 2
                    if (now - i > 1) {
                        i = now;
                        int r = random(150, 500);
                        analogWrite(SPEAKER_PIN, 128, r);
                    }
                    break;
                }
                case 8: {
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
                case 9: {
                    // WIN SOUND (Hard coded BLOCKING music player, sorry... ran out of time! :D)
                    #define NOTE_LEN (128)
                    #define NOTE_RES (40)
                    analogWrite(SPEAKER_PIN, 128, 740);
                    delay(NOTE_LEN-NOTE_RES); analogWrite(SPEAKER_PIN, 0, 740); delay(NOTE_RES);
                    analogWrite(SPEAKER_PIN, 128, 740);
                    delay(NOTE_LEN-NOTE_RES); analogWrite(SPEAKER_PIN, 0, 740); delay(NOTE_RES);
                    analogWrite(SPEAKER_PIN, 128, 740);
                    delay(NOTE_LEN-NOTE_RES); analogWrite(SPEAKER_PIN, 0, 740); delay(NOTE_RES);
                    analogWrite(SPEAKER_PIN, 128, 932);
                    delay((NOTE_LEN*2)-NOTE_RES); analogWrite(SPEAKER_PIN, 0, 932); delay(NOTE_RES);
                    analogWrite(SPEAKER_PIN, 128, 831);
                    delay((NOTE_LEN*2)-NOTE_RES); analogWrite(SPEAKER_PIN, 0, 831); delay(NOTE_RES);
                    analogWrite(SPEAKER_PIN, 128, 740);
                    delay((NOTE_LEN*2)-NOTE_RES); analogWrite(SPEAKER_PIN, 0, 740); delay(NOTE_RES);
                    analogWrite(SPEAKER_PIN, 128, 698);
                    delay((NOTE_LEN*2)-NOTE_RES); analogWrite(SPEAKER_PIN, 0, 698); delay(NOTE_RES);
                    analogWrite(SPEAKER_PIN, 128, 554);
                    delay(NOTE_LEN-NOTE_RES); analogWrite(SPEAKER_PIN, 0, 554); delay(NOTE_RES);
                    analogWrite(SPEAKER_PIN, 128, 740);
                    delay(NOTE_LEN-NOTE_RES); analogWrite(SPEAKER_PIN, 0, 740); delay(NOTE_RES);
                    analogWrite(SPEAKER_PIN, 128, 0);
                    delay(NOTE_LEN-NOTE_RES); analogWrite(SPEAKER_PIN, 0, 0); delay(NOTE_RES);
                    break;
                }
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
    static const uint32_t colors[7] = {0x00640064, 0x00009050, 0x00646400, 0x000000FF, 0x00640000, 0x00006400, 0x00BBBBBB}; // magenta, cyan, yellow, blue, red, green, white

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
            playSound(sound, SOUND_STATE_NEW); // reset sound
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
            playSound(sound, SOUND_STATE_PLAYING);
            break;
        }
        case DIE_ROLL_STATE_ROLLING: {
            if (!playSound(sound, SOUND_STATE_PLAYING) && now - startRoll >= DIE_ROLL_TIMEOUT_MS) {
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
            break;
        }
        case DIE_ROLL_STATE_FINISHED: {
            setDieNum(finalRandomNumber, color);
            state = DIE_ROLL_STATE_IDLE;
            break;
        }
        case DIE_ROLL_STATE_IDLE:
        default: {
        }
    }

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

    RGB.color(0,50,50);
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
    uint8_t r, g, b;
    uint32_t c;
    #define ITERATIONS (16)
    for (int f = 0; f < ITERATIONS; f++) {
        for (int i = 0; i < strip.numPixels(); i++) {
            c = strip.getPixelColor(i);
            r = (c >> 16) & 0xff;
            g = (c >> 8) & 0xff;
            b = c & 0xff;
            if (f == (ITERATIONS-1)) {
                r = 0;
                g = 0;
                b = 0;
            } else if (f > 12) {
                r -= r/2;
                g -= g/2;
                b -= b/2;
            } else if (f > 8) {
                r -= r/4;
                g -= g/4;
                b -= b/4;
            } else {
                // r -= r/8;
                // g -= g/8;
                // b -= b/8;
            }
            strip.setPixelColor(i, r, g, b);
        }
        // Serial.printlnf("%d, %d, %d", r, g, b);
        strip.show();
        delay(wait/ITERATIONS);
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
        switch (colorPick) {
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

int buttonPressed() {
    int button = 0;
    if (wakeupPin != PIN_INVALID) {
        if (wakeupPin == BUTTON_1_PIN) {
            button = 1;
        } else if (wakeupPin == BUTTON_2_PIN) {
            button = 2;
        } else if (wakeupPin == BUTTON_3_PIN) {
            button = 3;
        } else if (wakeupPin == BUTTON_4_PIN) {
            button = 4;
        } else if (wakeupPin == IR_RX_PIN) {
            button = 5;
        }
        // pressed = true;
        wakeupPin = PIN_INVALID;
    } else if (digitalRead(BUTTON_1_PIN) == LOW) {
        button = 1;
    } else if (digitalRead(BUTTON_2_PIN) == LOW) {
        button = 2;
    } else if (digitalRead(BUTTON_3_PIN) == LOW) {
        button = 3;
    } else if (digitalRead(BUTTON_4_PIN) == LOW) {
        button = 4;
    }
    // if (digitalRead(BUTTON_1_PIN) == LOW || digitalRead(BUTTON_2_PIN) == LOW || digitalRead(BUTTON_3_PIN) == LOW || digitalRead(BUTTON_4_PIN) == LOW) {
    //     pressed = true;
    // }
    return button;
}

void setup() {
    Serial.begin();

    RGB.control(true);
    RGB.color(0,0,0);

    // irrecv.enableIRIn(); // Start the receiver
    irsend.enableIROut(38);
    pinMode(PIXEL_ENABLE_PIN, OUTPUT);
    digitalWrite(PIXEL_ENABLE_PIN, HIGH);
    strip.begin();
    strip.setBrightness(128);
    strip.show(); // Initialize all pixels to 'off'

    pinMode(BUTTON_1_PIN, INPUT_PULLUP);
    pinMode(BUTTON_2_PIN, INPUT_PULLUP);
    pinMode(BUTTON_3_PIN, INPUT_PULLUP);
    pinMode(BUTTON_4_PIN, INPUT_PULLUP);

    pinMode(SPEAKER_PIN, OUTPUT);
    analogWrite(SPEAKER_PIN, 0, 1000);

    // If all buttons pressed, re-init score
    if (digitalRead(BUTTON_1_PIN) == LOW && digitalRead(BUTTON_2_PIN) == LOW && digitalRead(BUTTON_3_PIN) == LOW && digitalRead(BUTTON_4_PIN) == LOW) {
        bool cancelReset = false;
        uint32_t s = millis();
        while (millis() - s < 3000 && !cancelReset) {
            // If we let go of any button in 3 seconds, cancel the reset
            if ( !(digitalRead(BUTTON_1_PIN) == LOW && digitalRead(BUTTON_2_PIN) == LOW && digitalRead(BUTTON_3_PIN) == LOW && digitalRead(BUTTON_4_PIN) == LOW)) {
                cancelReset = true;
            }
        }
        if (!cancelReset) {
            initEEPROM();
            for (int x = 0; x<3; x++) {
                RGB.color(0,150,150);
                delay(200);
                RGB.color(0,0,0);
                delay(200);
            }
        }
    } // If buttons 2,3,4 pressed, enter test mode (press each button to verify buttons, leds and IR send/receive works)
    else if (digitalRead(BUTTON_1_PIN) == HIGH && digitalRead(BUTTON_2_PIN) == LOW && digitalRead(BUTTON_3_PIN) == LOW && digitalRead(BUTTON_4_PIN) == LOW) {
        bool cancelTestMode = false;
        uint32_t s = millis();
        while (millis() - s < 1000 && !cancelTestMode) {
            // If we let go of any button in 3 seconds, cancel test mode
            if ( !(digitalRead(BUTTON_1_PIN) == HIGH && digitalRead(BUTTON_2_PIN) == LOW && digitalRead(BUTTON_3_PIN) == LOW && digitalRead(BUTTON_4_PIN) == LOW)) {
                cancelTestMode = true;
            }
        }
        if (!cancelTestMode) RGB.color(0,255,0);
        while (buttonPressed());
        if (!cancelTestMode) {
            RGB.color(0,0,0);
            uint8_t buttons = 0;
            while (buttons != 0x1f) {
                while (buttonPressed() == 0);
                uint8_t b = buttonPressed();
                setDieNum(b+2, b);
                buttons |= (1 << (b-1)); // save button
                memset(dataBuf, 0, DATA_BUF_LEN);
                createMessage(dataBuf, MESSAGE_TYPE_RESULT, 0, 0, winner_id);
                irrecv.enableIRIn(); // Let's cature our own data for testing
                irsend.sendBytes(dataBuf, MESSAGE_TYPE_RESULT_LEN);
                delay(150);
                int ir_res = irrecv.decode(&irResults);
                if (ir_res) {
                    if (irResults.decode_type == BYTES) {
                        if (parse(&irResults) == -2 /* my own ID */) {
                            buttons |= (1 << 4); // save button
                        }
                    }
                    irrecv.resume(); // make sure to clear and re-enable IR after all tests above
                }
                while (buttonPressed());
            }
            for (int x=0; x<10; x++) {
                analogWrite(SPEAKER_PIN, 128, 800);
                delay(20);
                analogWrite(SPEAKER_PIN, 128, 900);
                delay(20);
            }
            analogWrite(SPEAKER_PIN, 0, 800);
            setDieNum(0, 0);
        }
    }
    else if (digitalRead(BUTTON_1_PIN) == LOW || digitalRead(BUTTON_2_PIN) == LOW || digitalRead(BUTTON_3_PIN) == LOW || digitalRead(BUTTON_4_PIN) == LOW) {
        muteSound = true;
        for (int x = 0; x<2; x++) {
            RGB.color(0,150,150);
            delay(200);
            RGB.color(0,0,0);
            delay(200);
        }
    }

    RGB.color(0,150,150);

    BLE.off();
    WiFi.off();
    WiFi.clearCredentials();

    loadScore();

#if ENABLE_ON_BOARD_SHT31
    delay(5000);
    Serial.println("SHT31 Example");
    if (!sht31.begin(0x44)) {
        Serial.println("Couldn't find SHT31");
    }
    for (int x = 0; x < 10; x++) {
        float t = sht31.readTemperature();
        float h = sht31.readHumidity();
        float tF = ((t * 9) / 5) + 32;
        if (!isnan(t)) {
            Serial.printlnf("Temperature = %0.2f°C / %0.2f°F", t, tF);
        } else {
            Serial.println("Failed to read temperature");
        }
        if (!isnan(h)) {
            Serial.printlnf("Humidity = %0.2f%%", h);
        } else {
            Serial.println("Failed to read humidity");
        }
        delay(1000);
    }
#endif // ENABLE_ON_BOARD_SHT31

#if ENABLE_QWIIC_SENSOR_DEMO
    delay(5000);
    Serial.println("VCNL4040 Ambient Light Sensor Example");
    Wire.begin();
    proximitySensor.begin(); //Initialize the sensor
    proximitySensor.powerOffProximity(); //Power down the proximity portion of the sensor
    proximitySensor.powerOnAmbient(); //Power up the ambient sensor
    for (int x = 0; x < 10; x++) {
        unsigned int ambientValue = proximitySensor.getAmbient();
        Serial.printlnf("Ambient light level: %d", ambientValue);
        delay(1000);
    }
#endif // ENABLE_QWIIC_SENSOR_DEMO
}


// #define MSG_HDR_OFF    (0)
// #define MSG_ID1_OFF    (1)
//                      //(2)
//                      //(3)
//                      //(4)
// #define MSG_MSG1_OFF   (5)
// #define MSG_WIN_ID_OFF (6)
// #define MSG_SCR1_OFF   (6)
// #define MSG_SCR2_OFF   (7)
// #define MSG_ID2_OFF    (8)
//                      //(9)
//                      //(10)
//                      //(11)
// #define MSG_MSG2_OFF   (12)

// #define MSG_HDR_OFF    (0)
#define MSG_ID1_OFF    (0)
                     //(1)
                     //(2)
                     //(3)
#define MSG_MSG1_OFF   (4)
#define MSG_WIN_ID_OFF (5)
#define MSG_SCR1_OFF   (5)
#define MSG_SCR2_OFF   (6)
#define MSG_ID2_OFF    (7)
                     //(8)
                     //(9)
                     //(10)
#define MSG_MSG2_OFF   (11)
void createMessage(uint8_t* buf, int msgType, int button, int strength, uint32_t id, void* player2msg) {
    uint32_t id32bit = deviceID_last4();
    // buf[0] = MAGIC_HEADER_BYTE; // magic header byte, ó - looks like a little waterballon
    memcpy(&buf[MSG_ID1_OFF], &id32bit, 4);
    buf[MSG_SCR1_OFF] = player1score >> 8;
    buf[MSG_SCR2_OFF] = player1score & 0xff;
    // Serial.printlnf("msgType:%d", msgType);
    switch (msgType) {
        case MESSAGE_TYPE_ATTACK: {
            buf[MSG_MSG1_OFF] = (MESSAGE_TYPE_ATTACK << MESSAGE_TYPE_OFFSET) + (button << MESSAGE_BUTTON_OFFSET) + (strength);
            break;
        }
        case MESSAGE_TYPE_ATTACK_ACK: {
            buf[MSG_MSG1_OFF] = (MESSAGE_TYPE_ATTACK_ACK << MESSAGE_TYPE_OFFSET) + (button << MESSAGE_BUTTON_OFFSET) + (strength);
            break;
        }
        case MESSAGE_TYPE_COUNTER_ATTACK: {
            buf[MSG_MSG1_OFF] = (MESSAGE_TYPE_COUNTER_ATTACK << MESSAGE_TYPE_OFFSET) + (button << MESSAGE_BUTTON_OFFSET) + (strength);
            memcpy(&buf[MSG_ID2_OFF], &id, 4);
            if (player2msg) {
                buf[MSG_MSG2_OFF] = *((uint8_t*)player2msg);
                // Serial.printlnf("player2msg:%02x", buf[12]);
            }
            break;
        }
        case MESSAGE_TYPE_COUNTER_ATTACK_ACK: {
            buf[MSG_MSG1_OFF] = (MESSAGE_TYPE_COUNTER_ATTACK_ACK << MESSAGE_TYPE_OFFSET) + (button << MESSAGE_BUTTON_OFFSET) + (strength);
            break;
        }
        case MESSAGE_TYPE_RESULT: {
            buf[MSG_MSG1_OFF] = (MESSAGE_TYPE_RESULT << MESSAGE_TYPE_OFFSET) + (button << MESSAGE_BUTTON_OFFSET) + (strength);
            // Serial.printlnf("MESSAGE_TYPE_RESULT buf[MSG_MSG1_OFF]:%02X %02X %02X", buf[MSG_MSG1_OFF], (MESSAGE_TYPE_RESULT << MESSAGE_TYPE_OFFSET), (MESSAGE_TYPE_COUNTER_ATTACK_ACK << MESSAGE_TYPE_OFFSET) + (button << MESSAGE_BUTTON_OFFSET) + (strength));
            memcpy(&buf[MSG_WIN_ID_OFF], &id, 4); // should be the winner_id
            break;
        }
        case MESSAGE_TYPE_RESULT_ACK: {
            buf[MSG_MSG1_OFF] = (MESSAGE_TYPE_RESULT_ACK << MESSAGE_TYPE_OFFSET) + (button << MESSAGE_BUTTON_OFFSET) + (strength);
            // Serial.printlnf("MESSAGE_TYPE_RESULT_ACK buf[MSG_MSG1_OFF]:%02X %02X %02X", buf[MSG_MSG1_OFF], (MESSAGE_TYPE_RESULT << MESSAGE_TYPE_OFFSET), (MESSAGE_TYPE_COUNTER_ATTACK_ACK << MESSAGE_TYPE_OFFSET) + (button << MESSAGE_BUTTON_OFFSET) + (strength));
            memcpy(&buf[MSG_WIN_ID_OFF], &id, 4); // should be the winner_id
            break;
        }
        case MESSAGE_TYPE_SCORE_ACK: {
            buf[MSG_MSG1_OFF] = (MESSAGE_TYPE_SCORE_ACK << MESSAGE_TYPE_OFFSET) + (button << MESSAGE_BUTTON_OFFSET) + (strength);
            break;
        }
        default: {
            break;
        }
    }

    if (msgType == MESSAGE_TYPE_ATTACK || msgType == MESSAGE_TYPE_COUNTER_ATTACK) {
        player1msg.type = msgType;
        player1msg.button = button;
        player1msg.strength = strength;
    }
}

void resetGame() {
    gameStateP1 = GAMEPLAY_STATE_IDLE;
    gameStateP2 = GAMEPLAY_STATE_IDLE;
    colorPick = 0;
    winner_id = 0;
    gameResult = GAME_RESULT_INVALID;
    RGB.color(0, 150, 150);
    digitalWrite(PIXEL_ENABLE_PIN, LOW);
}

int checkGameResults() {
    int gameResult = GAME_RESULT_INVALID;

    // Serial.printlnf("(US) player1id:%08lX %d [received_winner:%08lX winner_id:%08lX ] player2id:%08lX %d (THEM)", deviceID_last4(), player1msg.strength, received_winner_id, winner_id, player2id, player2msg.strength);
    uint8_t index = findPlayerSaveSlot(player2id);
    if (eeData.ids[index][EEPROM_PLAYER_PLAYS_OFFSET] >= EEPROM_PLAYER_PLAYS_MAX) {
        Serial.println("TOO MANY PLAYS WITH THIS PLAYER, FIND MORE PLAYERS!");
        gameResult = GAME_RESULT_INVALID;
        setDieNum(player1msg.strength, DIE_COLOR_WHITE);
        playSound(5, SOUND_STATE_NEW);

        return gameResult;
    }

    Serial.printlnf("PLAYER SLOT: %d, PLAYS: %lu", index, eeData.ids[index][EEPROM_PLAYER_PLAYS_OFFSET]);
    eeData.ids[index][EEPROM_PLAYER_PLAYS_OFFSET]++;

    // Validate winner_id received against our own calculation
    if (received_winner_id == winner_id) {
        if (deviceID_last4() == winner_id) {
            Serial.printlnf("++++ WIN! ++++");
            gameResult = GAME_RESULT_WIN;
            setDieNum(player1msg.strength, DIE_COLOR_GREEN);
            playSound(9, SOUND_STATE_NEW);

            if (player1score < 64990) { // 65000 max!
                player1score += 10;
            }

            saveScore(); // SAVE OUR PRECIOUS SCORE DATA!!
        } else if (player2id == winner_id) {
            Serial.printlnf("____ LOSE ____");
            gameResult = GAME_RESULT_LOSE;
            setDieNum(player1msg.strength, DIE_COLOR_RED);
            playSound(6, SOUND_STATE_NEW);

            saveScore(); // SAVE HERE MOSTLY TO KEEP THE PLAYS COUNT IN SYNC
        } else if (GAME_IS_A_DRAW_ID == winner_id) {
            Serial.printlnf("~~~~ DRAW ~~~~");
            gameResult = GAME_RESULT_DRAW;
            setDieNum(player1msg.strength, DIE_COLOR_BLUE);
            playSound(5, SOUND_STATE_NEW);

            saveScore(); // SAVE HERE MOSTLY TO KEEP THE PLAYS COUNT IN SYNC
        } else {
            Serial.printlnf("INVALID WINNER RESULTS!!");
            gameResult = GAME_RESULT_INVALID;
            setDieNum(player1msg.strength, DIE_COLOR_WHITE);
            // playSound(6, SOUND_STATE_NEW);
        }
    } else {
        Serial.printlnf("INVALID WINNER RESULTS!!");
    }

    return gameResult;
}

void processMessage() {
    extendWakeTime();
    switch (irDataRx.msg.type) {
        case MESSAGE_TYPE_ATTACK: {
            Serial.printlnf("PROCESS ATTACK");
            // Save P2 data
            player2id = irDataRx.id;
            player2msg = irDataRx.msg;

            digitalWrite(PIXEL_ENABLE_PIN, HIGH);
            delay(10);

            setDieNum(player2msg.strength, player2msg.button+1);
            playSound(player2msg.button+1+4, SOUND_STATE_NEW);
            // Serial.printlnf("button: %u, strength: %u", player2msg.button+1, player2msg.strength);

            badgeState = BADGE_STATE_SPLASH;
            gameStateP2 = GAMEPLAY_STATE_ATTACK;
            gameStateP1 = GAMEPLAY_STATE_ATTACK_ACK;
            startGamestateTimer = millis();

            RGB.color(0, 0, 0);

            // memset(dataBuf, 0, DATA_BUF_LEN);
            // createMessage(dataBuf, MESSAGE_TYPE_ATTACK_ACK, irDataRx.msg.button, irDataRx.msg.strength);
            // irrecv.disableIRIn();
            // irsend.sendBytes(dataBuf, MESSAGE_TYPE_ATTACK_ACK_LEN);
            // irrecv.enableIRIn();
            // Serial.printlnf("QUICK ATTACK_ACK");
            break;
        }
        case MESSAGE_TYPE_ATTACK_ACK: {
            // if (gameStateP2 == GAMEPLAY_STATE_ATTACK) {
            //     gameStateP1 = GAMEPLAY_STATE_RESULT;
            //     badgeState = BADGE_STATE_IDLE;
            // }
            break;
        }
        case MESSAGE_TYPE_COUNTER_ATTACK: {
            Serial.printlnf("PROCESS COUNTER ATTACK");
            // Save P2 data
            player2id = irDataRx.id;
            player2msg = irDataRx.msg;

            digitalWrite(PIXEL_ENABLE_PIN, HIGH);
            delay(10);

            setDieNum(player2msg.strength, player2msg.button+1);
            playSound(player2msg.button+1+4, SOUND_STATE_NEW);
            // Serial.printlnf("button: %u, strength: %u", player2msg.button+1, player2msg.strength);

            badgeState = BADGE_STATE_SPLASH;
            gameStateP2 = GAMEPLAY_STATE_COUNTER_ATTACK;

            RGB.color(0, 0, 0);

            // determine winner ahead of time
            if (player1msg.strength > player2msg.strength) {
                winner_id = deviceID_last4();
            } else if (player1msg.strength < player2msg.strength) {
                winner_id = player2id;
            } else {
                winner_id = GAME_IS_A_DRAW_ID;
            }

            break;
        }
        case MESSAGE_TYPE_RESULT: {
            Serial.printlnf("PROCESS RESULT");

            retransmits = 1;
            startRetransmit = millis() - (retransmitDelay*2); // Send immediately!
            memset(dataBuf, 0, DATA_BUF_LEN);
            createMessage(dataBuf, MESSAGE_TYPE_RESULT_ACK, 0, 0, winner_id);

            gameResult = checkGameResults();

            badgeState = BADGE_STATE_DISPLAY_RESULT;
            gameStateP2 = GAMEPLAY_STATE_RESULT_DISPLAY;
            gameStateP1 = GAMEPLAY_STATE_RESULT_ACK;

            break;
        }
        case MESSAGE_TYPE_RESULT_ACK: {
            Serial.printlnf("PROCESS RESULT_ACK");

            gameResult = checkGameResults();

            badgeState = BADGE_STATE_DISPLAY_RESULT;
            gameStateP1 = GAMEPLAY_STATE_RESULT_DISPLAY;

            break;
        }
        case MESSAGE_TYPE_SCORE_ACK: {
            Serial.printlnf("PROCESS RESULT_ACK");

            badgeState = BADGE_STATE_IDLE;
            resetGame();

            break;
        }
        default: {
        }
    }
    irDataRx.valid = 0; // message processed
}

void loop() {
    switch (badgeState) {
        case BADGE_STATE_IDLE: {
            // READ AND DECODE INCOMING IR
            int ir_res = irrecv.decode(&irResults);
            if (ir_res) {
                if (irResults.decode_type == BYTES) {
                    if (parse(&irResults) == 0 && irDataRx.valid) {
                        badgeState = BADGE_STATE_IDLE;
                        // Serial.printlnf("irDataRx.msg:%02X, irDataRx.msg.type:%02X", *((uint8_t *)&irDataRx.msg), irDataRx.msg.type);
                        processMessage();
                    }
                }
                irrecv.resume(); // make sure to clear and re-enable IR after all tests above
                if (badgeState == BADGE_STATE_IDLE) {
                    break;
                }
            } else {
                // Serial.printlnf("ir_res: %d", ir_res);
            }

            // if (millis() - lastSleep >= SLEEP_TIMEOUT_MS) {
            //     badgeState = BADGE_STATE_SLEEP;
            //     break;
            // }

            // CHECK FOR BUTTON PRESSES
            int btn = buttonPressed();
            if (btn) {
                colorPick = btn;

                if (colorPick == 5) {
                    // rainbow(4); // TODO: blocking, probably just remove this and wait for repeated command to display something
                    break;
                }

                digitalWrite(PIXEL_ENABLE_PIN, HIGH);
                delay(10);

                if (gameStateP1 == GAMEPLAY_STATE_IDLE || gameStateP1 == GAMEPLAY_STATE_ATTACK_ACK) {
                    badgeState = BADGE_STATE_DIE_ROLL_INIT;
                }
            }

            break;
        }
        case BADGE_STATE_SLEEP: {
            sleepAfterDelay();
            badgeState = BADGE_STATE_WAKEUP;
            break;
        }
        case BADGE_STATE_WAKEUP: {
            badgeState = BADGE_STATE_IDLE;
            break;
        }
        case BADGE_STATE_MESSAGE_AVAILABLE: {
            badgeState = BADGE_STATE_IDLE;
            break;
        }
        case BADGE_STATE_DIE_ROLL_INIT: {
            randomNumber = random(6)+1;
            dieRoll(randomNumber, colorPick, true); // force new roll
            // Serial.printlnf("START RAND: %d", randomNumber);
            startDieRoll = millis();
            rolling = 1;
            badgeState = BADGE_STATE_DIE_ROLL;
            break;
        }
        case BADGE_STATE_DIE_ROLL: {
            if (rolling && millis() - startDieRoll < DIE_ROLL_TIMEOUT_MAX_MS) {
                rolling = dieRoll(randomNumber, colorPick);
            }
            if (!rolling) {
                badgeState = BADGE_STATE_IDLE;
                retransmits = 1;
                startRetransmit = millis() - (retransmitDelay*2); // Send immediately!
                memset(dataBuf, 0, DATA_BUF_LEN);
                if (gameStateP2 == GAMEPLAY_STATE_IDLE) {
                    gameStateP1 = GAMEPLAY_STATE_ATTACK;
                    createMessage(dataBuf, MESSAGE_TYPE_ATTACK, colorPick-1, randomNumber);
                } else if (gameStateP2 == GAMEPLAY_STATE_ATTACK) {
                    gameStateP1 = GAMEPLAY_STATE_COUNTER_ATTACK;
                    createMessage(dataBuf, MESSAGE_TYPE_COUNTER_ATTACK, colorPick-1, randomNumber, player2id, &player2msg);

                    // determine winner ahead of time
                    if (player1msg.strength > player2msg.strength) {
                        winner_id = deviceID_last4();
                    } else if (player1msg.strength < player2msg.strength) {
                        winner_id = player2id;
                    } else {
                        winner_id = GAME_IS_A_DRAW_ID;
                    }
                }
                startGamestateTimer = 0;
            }
            break;
        }
        case BADGE_STATE_SPLASH: {
            if (!playSound(irDataRx.msg.button+1+4, SOUND_STATE_PLAYING)) {
                // retransmits = 3;
                // startRetransmit = 0;
                // memcpy(&irDataRxCurrent, &irDataRx, sizeof(IRData));
                // memset(dataBuf, 0, DATA_BUF_LEN);
                // createMessage(dataBuf, MESSAGE_TYPE_ATTACK_ACK, irDataRxCurrent.msg.button, irDataRxCurrent.msg.strength);
                // gameStateP1 = GAMEPLAY_STATE_ATTACK_ACK;

                if (gameStateP2 == GAMEPLAY_STATE_COUNTER_ATTACK) {
                    retransmits = 1;
                    startRetransmit = millis() - (retransmitDelay*2); // Send immediately!
                    memset(dataBuf, 0, DATA_BUF_LEN);
                    gameStateP1 = GAMEPLAY_STATE_RESULT;
                    createMessage(dataBuf, MESSAGE_TYPE_RESULT, 0, 0, winner_id);
                }

                fadeOut(1000); // blocking
                badgeState = BADGE_STATE_IDLE;
            }
            break;
        }
        case BADGE_STATE_DISPLAY_RESULT: {
            int sound = 0;
            if (gameResult == GAME_RESULT_WIN) {
                sound = 9;
            } else if (gameResult == GAME_RESULT_LOSE) {
                sound = 6;
            } else {
                sound = 5;
            }
            if (!playSound(sound, SOUND_STATE_PLAYING)) {
                fadeOut(2000); // blocking
                badgeState = BADGE_STATE_IDLE;
                resetGame();
                Serial.printlnf("GAME FINISHED");
            }
            break;
        }
        default: {
            break;
        }
    }


    switch (gameStateP1) {
        case GAMEPLAY_STATE_IDLE: {
            break;
        }
        case GAMEPLAY_STATE_ATTACK: {

            if ((retransmits > 0) && (millis() - startRetransmit > retransmitDelay)) {

                irrecv.disableIRIn();
                irsend.sendBytes(dataBuf, MESSAGE_TYPE_ATTACK_LEN);
                irrecv.enableIRIn();

                Serial.printlnf("ATTACK:%d", retransmits);

                // if (retransmits == 10) {
                    fadeOut(500);
                // } else {
                    retransmitDelay = 500; // * (random(4)+1);
                    startRetransmit = millis();
                // }
                // while (buttonPressed());
                // playSound(colorPick, SOUND_STATE_FINISHED); // ensure sound has stopped
                extendWakeTime();
                retransmits--;
                if (retransmits == 0) {
                    startGamestateTimer = millis();
                }
            }
            if (startGamestateTimer && (millis() - startGamestateTimer > GAME_STATE_TIMEOUT_MS)) {
                startGamestateTimer = 0;
                resetGame();
                Serial.printlnf("ATTACK TIMEOUT");
            }
            break;
        }
        case GAMEPLAY_STATE_ATTACK_ACK: {

            if (startGamestateTimer && (millis() - startGamestateTimer > GAME_STATE_TIMEOUT_MS)) {
                startGamestateTimer = 0;
                resetGame();
                Serial.printlnf("ATTACK_ACK TIMEOUT");
            }
            break;
        }
        case GAMEPLAY_STATE_COUNTER_ATTACK: {

            if ((retransmits > 0) && (millis() - startRetransmit > retransmitDelay)) {

                irrecv.disableIRIn();
                irsend.sendBytes(dataBuf, MESSAGE_TYPE_COUNTER_ATTACK_LEN);
                irrecv.enableIRIn();

                Serial.printlnf("COUNTER:%d", retransmits);

                // if (retransmits == 10) {
                    fadeOut(500);
                // } else {
                    retransmitDelay = 500; // * (random(4)+1);
                    startRetransmit = millis();
                // }
                // while (buttonPressed());
                // playSound(colorPick, SOUND_STATE_FINISHED); // ensure sound has stopped
                extendWakeTime();
                retransmits--;
                if (retransmits == 0) {
                    startGamestateTimer = millis();
                }
            }
            if (startGamestateTimer && (millis() - startGamestateTimer > GAME_STATE_TIMEOUT_MS)) {
                startGamestateTimer = 0;
                resetGame();
                Serial.printlnf("COUNTER TIMEOUT");
            }
            break;
        }
        case GAMEPLAY_STATE_COUNTER_ATTACK_ACK: {
            break;
        }
        case GAMEPLAY_STATE_RESULT: {

            if ((retransmits > 0) && (millis() - startRetransmit > retransmitDelay)) {

                irrecv.disableIRIn();
                irsend.sendBytes(dataBuf, MESSAGE_TYPE_RESULT_LEN);
                irrecv.enableIRIn();

                Serial.printlnf("RESULT:%d", retransmits);

                // if (retransmits == 1) {
                    // fadeOut(1000);
                // } else {
                    retransmitDelay = 500; // * (random(4)+1);
                    startRetransmit = millis();
                // }
                // while (buttonPressed());
                // playSound(colorPick, SOUND_STATE_FINISHED); // ensure sound has stopped
                extendWakeTime();
                retransmits--;
                if (retransmits == 0) {
                    startGamestateTimer = millis();
                }
            }
            if (startGamestateTimer && (millis() - startGamestateTimer > GAME_STATE_TIMEOUT_MS)) {
                startGamestateTimer = 0;
                resetGame();
                Serial.printlnf("RESULT TIMEOUT");
            }
            break;
        }
        case GAMEPLAY_STATE_RESULT_ACK: {

            if ((retransmits > 0) && (millis() - startRetransmit > retransmitDelay)) {

                irrecv.disableIRIn();
                irsend.sendBytes(dataBuf, MESSAGE_TYPE_RESULT_ACK_LEN);
                irrecv.enableIRIn();

                Serial.printlnf("RESULT_ACK:%d", retransmits);

                // if (retransmits == 10) {
                    // fadeOut(1000);
                // } else {
                    retransmitDelay = 500; // * (random(4)+1);
                    startRetransmit = millis();
                // }
                // while (buttonPressed());
                // playSound(colorPick, SOUND_STATE_FINISHED); // ensure sound has stopped
                extendWakeTime();
                retransmits--;
                if (retransmits == 0) {
                    // startGamestateTimer = millis();

                    startGamestateTimer = 0;
                    gameStateP1 = GAMEPLAY_STATE_IDLE;
                    gameStateP2 = GAMEPLAY_STATE_IDLE;
                    colorPick = 0;
                    winner_id = 0;
                }
            }
            if (startGamestateTimer && (millis() - startGamestateTimer > GAME_STATE_TIMEOUT_MS)) {
                startGamestateTimer = 0;
                resetGame();
                Serial.printlnf("RESULT_ACK TIMEOUT");
            }
            break;
        }
        case GAMEPLAY_STATE_RESULT_DISPLAY: {
            // temp gameplay state while we display results
            break;
        }
        case GAMEPLAY_STATE_SCORE_ACK: {
            break;
        }
        default: {
            break;
        }
    }


}
