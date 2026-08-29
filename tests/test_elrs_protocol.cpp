#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "../ELRSProtocol.h"

namespace {

int failures = 0;

template <typename Actual, typename Expected>
void expectEqual(const char *name, Actual actual, Expected expected) {
  if (actual != expected) {
    std::cerr << "FAIL " << name << ": expected " << +expected
              << ", got " << +actual << '\n';
    ++failures;
  }
}

void testKnownGoodFrameDecodesChannels() {
  // Independently packed CRSF fixture containing channels
  // [0, 100, 200, ..., 1500].
  const uint8_t frame[] = {
      0xC8, 0x18, 0x16, 0x00, 0x20, 0x03, 0x32, 0x58, 0x02,
      0x19, 0xFA, 0x60, 0x89, 0x57, 0x20, 0x23, 0x1C, 0xFA,
      0x98, 0x08, 0x4B, 0x8A, 0xE2, 0x95, 0xBB, 0xA6};

  elrs::CrsfParser parser;
  elrs::RcChannels channels{};
  bool decoded = false;
  for (uint8_t byte : frame) {
    decoded = parser.push(byte, 1000, channels) || decoded;
  }

  expectEqual("known frame decoded", decoded, true);
  expectEqual("channel 1", channels.values[0], uint16_t{0});
  expectEqual("channel 2", channels.values[1], uint16_t{100});
  expectEqual("channel 3", channels.values[2], uint16_t{200});
  expectEqual("channel 4", channels.values[3], uint16_t{300});
  expectEqual("channel 16", channels.values[15], uint16_t{1500});
}

void testCorruptFrameIsRejected() {
  uint8_t frame[] = {
      0xC8, 0x18, 0x16, 0xDF, 0xF5, 0x6E, 0x77, 0xB9, 0x5B,
      0x5E, 0xF2, 0x8E, 0x57, 0xBC, 0xE1, 0x05, 0xEF, 0x77,
      0xBD, 0xDB, 0x5D, 0xEE, 0xFA, 0x56, 0xF1, 0x1A};
  elrs::CrsfParser parser;
  elrs::RcChannels channels{};
  bool decoded = false;
  for (uint8_t byte : frame) {
    decoded = parser.push(byte, 1000, channels) || decoded;
  }
  expectEqual("bad CRC rejected", decoded, false);
}

void testAxisMappingUsesCrsfCalibration() {
  expectEqual("axis minimum", elrs::mapAxis(elrs::kChannelMin), int8_t{-127});
  expectEqual("axis center", elrs::mapAxis(elrs::kChannelCenter), int8_t{0});
  expectEqual("axis maximum", elrs::mapAxis(elrs::kChannelMax), int8_t{127});
  expectEqual("axis clamps low", elrs::mapAxis(0), int8_t{-127});
  expectEqual("axis clamps high", elrs::mapAxis(2047), int8_t{127});
  expectEqual("throttle minimum", elrs::mapThrottle(elrs::kChannelMin), int8_t{-127});
  expectEqual("throttle maximum", elrs::mapThrottle(elrs::kChannelMax), int8_t{127});
}

void testHidReportHasCenteredHat() {
  elrs::RcChannels channels{};
  for (auto &value : channels.values) value = elrs::kChannelCenter;
  const elrs::HidReport report = elrs::makeHidReport(channels);
  expectEqual("hat centered", report.hat, uint8_t{0});
  expectEqual("roll centered", report.x, int8_t{0});
  expectEqual("pitch centered", report.y, int8_t{0});
  expectEqual("yaw centered", report.rz, int8_t{0});
}

void testFailsafeRetriesUntilDeliveryIsConfirmed() {
  elrs::RcLinkState link(1000, 20);
  link.noteFrame(100);
  expectEqual("fresh link", link.shouldSendFailsafe(1099), false);
  expectEqual("timeout reached", link.shouldSendFailsafe(1100), true);
  expectEqual("retry rate limited", link.shouldSendFailsafe(1101), false);
  expectEqual("failed send retried", link.shouldSendFailsafe(1120), true);
  link.confirmFailsafeSent();
  expectEqual("confirmed failsafe stops retries", link.shouldSendFailsafe(1200), false);
  link.noteFrame(1300);
  expectEqual("new frame rearms failsafe", link.shouldSendFailsafe(2300), true);
}

void testFailsafeTimeoutHandlesMillisWraparound() {
  elrs::RcLinkState link(1000, 20);
  link.noteFrame(UINT32_MAX - 499);
  expectEqual("wrapped link still fresh", link.shouldSendFailsafe(499), false);
  expectEqual("wrapped timeout reached", link.shouldSendFailsafe(500), true);
}

void testFailsafeReportCentersAxesAndLowersThrottle() {
  const elrs::HidReport report = elrs::makeFailsafeReport();
  expectEqual("failsafe roll centered", report.x, int8_t{0});
  expectEqual("failsafe pitch centered", report.y, int8_t{0});
  expectEqual("failsafe yaw centered", report.rz, int8_t{0});
  expectEqual("failsafe throttle low", report.z, int8_t{-127});
  expectEqual("failsafe hat centered", report.hat, uint8_t{0});
}

void testInterByteTimeoutResynchronizesParser() {
  const uint8_t frame[] = {
      0xC8, 0x18, 0x16, 0xDF, 0xF5, 0x6E, 0x77, 0xB9, 0x5B,
      0x5E, 0xF2, 0x8E, 0x57, 0xBC, 0xE1, 0x05, 0xEF, 0x77,
      0xBD, 0xDB, 0x5D, 0xEE, 0xFA, 0x56, 0xF1, 0x1B};
  elrs::CrsfParser parser(2000);
  elrs::RcChannels channels{};

  parser.push(0xC8, 0, channels);
  parser.push(0x18, 20, channels);
  parser.push(0x16, 40, channels);

  bool decoded = false;
  uint32_t now = 3000;
  for (uint8_t byte : frame) {
    decoded = parser.push(byte, now, channels) || decoded;
    now += 20;
  }
  expectEqual("parser recovers after timeout", decoded, true);
}

}  // namespace

int main() {
  testKnownGoodFrameDecodesChannels();
  testCorruptFrameIsRejected();
  testAxisMappingUsesCrsfCalibration();
  testHidReportHasCenteredHat();
  testFailsafeRetriesUntilDeliveryIsConfirmed();
  testFailsafeTimeoutHandlesMillisWraparound();
  testFailsafeReportCentersAxesAndLowersThrottle();
  testInterByteTimeoutResynchronizesParser();

  if (failures == 0) {
    std::cout << "All ELRS protocol tests passed\n";
    return EXIT_SUCCESS;
  }
  std::cerr << failures << " test(s) failed\n";
  return EXIT_FAILURE;
}

