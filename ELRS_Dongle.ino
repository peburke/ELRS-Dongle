/*
 * CRSF to USB HID gamepad bridge for ESP32-S3.
 *
 * Connect the ELRS receiver TX pad to GPIO 3. GPIO 4 is reserved for
 * future CRSF telemetry and is not used by this receive-only firmware.
 */

#include <Arduino.h>

#ifndef ARDUINO_USB_MODE
#error "This firmware requires an ESP32 with native USB support."
#elif ARDUINO_USB_MODE == 1
#error "Select USB-OTG (TinyUSB), not Hardware CDC and JTAG, for USB HID."
#endif

#include <HardwareSerial.h>
#include "USB.h"
#include "USBHIDGamepad.h"

#include "ELRSProtocol.h"

static_assert(HAT_CENTER == 0, "ESP32 HID hat-center value changed");

namespace {

constexpr uint32_t kCrsfBaud = 420000;
constexpr uint8_t kCrsfUartNumber = 1;
constexpr int8_t kCrsfRxPin = 3;
constexpr int8_t kCrsfTxPin = 4;
constexpr uint32_t kParserInterByteTimeoutUs = 2000;
constexpr uint32_t kRcFailsafeTimeoutMs = 1000;
constexpr uint32_t kFailsafeRetryIntervalMs = 20;

USBHIDGamepad gamepad;
HardwareSerial crsfSerial(kCrsfUartNumber);
elrs::CrsfParser crsfParser(kParserInterByteTimeoutUs);
elrs::RcLinkState rcLink(kRcFailsafeTimeoutMs, kFailsafeRetryIntervalMs);

uint32_t hidSendFailures = 0;

bool sendHidReport(const elrs::HidReport &report) {
  if (!gamepad.send(report.x, report.y, report.z, report.rz,
                    report.rx, report.ry, report.hat, report.buttons)) {
    ++hidSendFailures;
    return false;
  }
  return true;
}

}  // namespace

void setup() {
  crsfSerial.begin(kCrsfBaud, SERIAL_8N1, kCrsfRxPin, kCrsfTxPin);

  USB.manufacturerName("ELRS Dongle");
  USB.productName("ELRS Simulator Gamepad");
  gamepad.begin();
  USB.begin();
}

void loop() {
  elrs::RcChannels channels{};

  while (crsfSerial.available() > 0) {
    const uint8_t byte = static_cast<uint8_t>(crsfSerial.read());
    if (crsfParser.push(byte, micros(), channels)) {
      rcLink.noteFrame(millis());
      sendHidReport(elrs::makeHidReport(channels));
    }
  }

  if (rcLink.shouldSendFailsafe(millis()) &&
      sendHidReport(elrs::makeFailsafeReport())) {
    rcLink.confirmFailsafeSent();
  }
}

