/*
 * CRSF → USB Joystick bridge for ESP32-S3
 *
 * - Reads CRSF frames from an ELRS / Crossfire receiver (UART @ 420000 baud)
 * - Parses RC Channels (type 0x16)
 * - Maps channels 1–4 to joystick axes (X/Y/Z/Rz) on a USB HID gamepad
 *
 * Tested API: ESP32 Arduino core 3.3.4
 * USBHIDGamepad has: bool send(int8_t x, int8_t y, int8_t z,
 *                              int8_t rz, int8_t rx, int8_t ry,
 *                              uint8_t hat, uint32_t buttons);
 */

#include <HardwareSerial.h>
#include "USB.h"
#include "USBHIDGamepad.h"

// ----------- CRSF SETTINGS -----------
#define CRSF_BAUD       420000
#define CRSF_UART_NUM   1         // Use UART1 (Serial1)
#define CRSF_RX_PIN     3        // <-- change to your wiring
#define CRSF_TX_PIN     4        // <-- optional, not required to just receive

// CRSF protocol constants
static const uint8_t CRSF_ADDR_RADIO        = 0xC8;
static const uint8_t CRSF_ADDR_FLIGHTCTRL   = 0xEA;
static const uint8_t CRSF_ADDR_BROADCAST    = 0xEE;

static const uint8_t CRSF_TYPE_RC_CHANNELS_PACKED = 0x16;

// RC channels packed frame: 16 channels, 11 bits each → 22 bytes payload
static const uint8_t CRSF_RC_CHANNELS_PAYLOAD_LEN = 22;

// Safety buffer size
static const uint8_t CRSF_MAX_FRAME_SIZE = 64;

// ----------- USB GAMEPAD -------------
USBHIDGamepad Gamepad;

HardwareSerial CrsfSerial(CRSF_UART_NUM);

// ----------- CRSF PARSER STATE MACHINE -----------

enum CrsfParseState {
  WAIT_ADDR,
  WAIT_LEN,
  WAIT_TYPE_AND_PAYLOAD
};

CrsfParseState crsfState = WAIT_ADDR;

uint8_t  crsfAddr        = 0;
uint8_t  crsfLength      = 0;      // includes TYPE + PAYLOAD + CRC
uint8_t  crsfBuffer[CRSF_MAX_FRAME_SIZE];
uint8_t  crsfIndex       = 0;      // index into buffer (TYPE + PAYLOAD + CRC)
uint8_t  crsfExpectedLen = 0;      // same as crsfLength

// ----------- CRC8 (poly 0xD5) for CRSF -----------

uint8_t crsfCrc8(const uint8_t *data, uint8_t len) {
  uint8_t crc = 0;
  while (len--) {
    crc ^= *data++;
    for (int i = 0; i < 8; i++) {
      if (crc & 0x80) {
        crc = (crc << 1) ^ 0xD5;
      } else {
        crc = (crc << 1);
      }
    }
  }
  return crc;
}

// ----------- CHANNEL UNPACKING -----------

/*
 * CRSF RC Channels:
 *  - TYPE: 0x16
 *  - PAYLOAD: 22 bytes, 16 channels, each 11 bits (packed LSB first)
 *
 * channels[0..15] output in raw 11-bit range (0..2047)
 */
void crsfDecodeRcChannels(const uint8_t *payload, uint16_t *channels16) {
  const uint8_t BYTES = CRSF_RC_CHANNELS_PAYLOAD_LEN;

  for (int ch = 0; ch < 16; ch++) {
    uint16_t bitIndex  = ch * 11;
    uint16_t byteIndex = bitIndex / 8;
    uint8_t  bitOffset = bitIndex % 8;

    // Build a 24-bit window starting at byteIndex (guarding boundaries)
    uint32_t val = 0;
    if (byteIndex < BYTES) {
      val |= (uint32_t)payload[byteIndex];
    }
    if ((byteIndex + 1) < BYTES) {
      val |= (uint32_t)payload[byteIndex + 1] << 8;
    }
    if ((byteIndex + 2) < BYTES) {
      val |= (uint32_t)payload[byteIndex + 2] << 16;
    }

    uint16_t chVal = (val >> bitOffset) & 0x7FF; // 11 bits
    channels16[ch] = chVal;
  }
}

// Map CRSF 11-bit channel (0..2047) to signed 8-bit joystick axis (-127..127)
int8_t mapChannelToAxisInt8(int32_t ch) {
  // Center ~1024; treat 0..2047 symmetrically
  int32_t centered = ch - 1024;          // roughly -1024..+1023
  // scale centered range to -127..+127
  int32_t axis = (centered * 127L) / 1024L;

  if (axis > 127) axis = 127;
  if (axis < -127) axis = -127;
  return (int8_t)axis;
}

// Optional: map CRSF channel to 0..127 for throttle-like axis
int8_t mapChannelToThrottleInt8(int32_t ch) {
  if (ch < 0)    ch = 0;
  if (ch > 2047) ch = 2047;
  // Map 0..2047 -> -127..+127 but shifted so bottom = -127, top = +127
  // Many sims calibrate anyway, so this will work fine.
  int32_t axis = ((ch * 254L) / 2047L) - 127;  // ~ -127..+127
  if (axis > 127) axis = 127;
  if (axis < -127) axis = -127;
  return (int8_t)axis;
}

// ----------- HANDLE COMPLETE CRSF FRAME -----------

void handleCrsfFrame(uint8_t addr, const uint8_t *frame, uint8_t length) {
  // frame[0] = TYPE
  // frame[1..len-2] = PAYLOAD
  // frame[len-1] = CRC (already verified)

  if (length < 2) return; // at least TYPE + CRC

  uint8_t type = frame[0];
  uint8_t payloadLen = length - 2; // strip TYPE and CRC

  // RC Channels packed
  if (type == CRSF_TYPE_RC_CHANNELS_PACKED &&
      payloadLen == CRSF_RC_CHANNELS_PAYLOAD_LEN) {

    uint16_t channels[16];
    crsfDecodeRcChannels(&frame[1], channels); // payload starts at frame[1]

    // Map channels to joystick:
    // Ch1: Roll  -> X
    // Ch2: Pitch -> Y
    // Ch3: Throttle -> Z
    // Ch4: Yaw   -> Rz

    int8_t axisX  = mapChannelToAxisInt8(channels[0]);
    int8_t axisY  = mapChannelToAxisInt8(channels[1]);
    int8_t axisZ  = mapChannelToThrottleInt8(channels[2]);
    int8_t axisRz = mapChannelToAxisInt8(channels[3]);

    // RX/RY unused for now
    int8_t axisRx = 0;
    int8_t axisRy = 0;

    // Hat:
    //  USBHIDGamepad expects: 0-7 for directions, 8 for released (neutral).
    uint8_t hat = 8; // neutral

    // Buttons:
    // 32 buttons as bitmask; we keep 0 for now.
    uint32_t buttons = 0;

    // ---- THIS IS THE IMPORTANT CHANGE ----
    // Using the API from USBHIDGamepad.h in ESP32 core 3.3.4:
    // bool send(int8_t x, int8_t y, int8_t z,
    //           int8_t rz, int8_t rx, int8_t ry,
    //           uint8_t hat, uint32_t buttons);
    Gamepad.send(axisX, axisY, axisZ,
                 axisRz, axisRx, axisRy,
                 hat, buttons);
  }
}

// ----------- CRSF BYTE-BY-BYTE PARSER -----------

void processCrsfByte(uint8_t b) {
  switch (crsfState) {
    case WAIT_ADDR:
      if (b == CRSF_ADDR_RADIO || b == CRSF_ADDR_FLIGHTCTRL || b == CRSF_ADDR_BROADCAST) {
        crsfAddr = b;
        crsfState = WAIT_LEN;
      }
      break;

    case WAIT_LEN:
      // Length includes TYPE + PAYLOAD + CRC
      crsfLength = b;
      if (crsfLength < 2 || crsfLength > (CRSF_MAX_FRAME_SIZE - 2)) {
        // Invalid length, reset parser
        crsfState = WAIT_ADDR;
      } else {
        crsfIndex = 0;
        crsfExpectedLen = crsfLength;
        crsfState = WAIT_TYPE_AND_PAYLOAD;
      }
      break;

    case WAIT_TYPE_AND_PAYLOAD:
      if (crsfIndex < CRSF_MAX_FRAME_SIZE) {
        crsfBuffer[crsfIndex++] = b;
      }

      if (crsfIndex >= crsfExpectedLen) {
        // We now have TYPE + PAYLOAD + CRC in crsfBuffer[0..crsfLength-1]
        // Check CRC:
        uint8_t crcCalculated = crsfCrc8(crsfBuffer, crsfExpectedLen - 1);
        uint8_t crcReceived   = crsfBuffer[crsfExpectedLen - 1];

        if (crcCalculated == crcReceived) {
          // Pass to handler (excluding address and length, which we already know)
          handleCrsfFrame(crsfAddr, crsfBuffer, crsfExpectedLen);
        }

        // Ready for next frame
        crsfState = WAIT_ADDR;
      }
      break;
  }
}

// ----------- ARDUINO SETUP & LOOP -----------

void setup() {
  // NOTE:
  //  - Your FQBN shows USBMode=default, CDCOnBoot=default, DFUOnBoot=dfu.
  //  - That sets ARDUINO_USB_MODE=0, which is "no native USB".
  //  - To make the gamepad work, in Tools → USB Mode, pick a mode that
  //    enables native USB HID (e.g. "USB-OTG" or similar for your board).

  CrsfSerial.begin(CRSF_BAUD, SERIAL_8N1, CRSF_RX_PIN, CRSF_TX_PIN);

  USB.begin();
  Gamepad.begin();

  delay(1000); // let USB enumerate
}

void loop() {
  // Pump CRSF bytes from UART into parser
  while (CrsfSerial.available()) {
    uint8_t b = (uint8_t)CrsfSerial.read();
    processCrsfByte(b);
  }
}
