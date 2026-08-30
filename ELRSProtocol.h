#pragma once

#include <cstddef>
#include <cstdint>

namespace elrs {

constexpr uint8_t kSyncByte = 0xC8;
constexpr uint8_t kRcChannelsPackedType = 0x16;
constexpr std::size_t kChannelCount = 16;
constexpr std::size_t kRcPayloadLength = 22;
constexpr uint8_t kMaxFrameBodyLength = 62;

constexpr uint16_t kChannelMin = 172;
constexpr uint16_t kChannelCenter = 992;
constexpr uint16_t kChannelMax = 1811;

struct RcChannels {
  uint16_t values[kChannelCount];
};

struct HidReport {
  int8_t x;
  int8_t y;
  int8_t z;
  int8_t rz;
  int8_t rx;
  int8_t ry;
  uint8_t hat;
  uint32_t buttons;
};

uint8_t crc8(const uint8_t *data, std::size_t length);
void decodeRcChannels(const uint8_t *payload, RcChannels &channels);
int8_t mapAxis(uint16_t channel);
int8_t mapThrottle(uint16_t channel);
HidReport makeHidReport(const RcChannels &channels);
HidReport makeFailsafeReport();

class CrsfParser {
 public:
  explicit CrsfParser(uint32_t interByteTimeoutUs = 2000);
  bool push(uint8_t byte, uint32_t nowUs, RcChannels &channels);
  void reset();

 private:
  enum class State : uint8_t { WaitSync, WaitLength, ReadBody };

  State state_;
  uint8_t expectedLength_;
  uint8_t index_;
  uint8_t body_[kMaxFrameBodyLength];
  uint32_t interByteTimeoutUs_;
  uint32_t lastByteUs_;
};

class RcLinkState {
 public:
  explicit RcLinkState(uint32_t timeoutMs, uint32_t retryIntervalMs = 20);
  void noteFrame(uint32_t nowMs);
  bool shouldSendFailsafe(uint32_t nowMs);
  void confirmFailsafeSent();

 private:
  uint32_t timeoutMs_;
  uint32_t retryIntervalMs_;
  uint32_t lastFrameMs_;
  uint32_t lastAttemptMs_;
  bool hasFrame_;
  bool hasAttempted_;
  bool failsafeSent_;
};

}  // namespace elrs

