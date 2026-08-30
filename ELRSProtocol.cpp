#include "ELRSProtocol.h"

namespace elrs {

namespace {

constexpr uint8_t kHidHatCenter = 0;

int8_t clampAxis(int32_t value) {
  if (value < -127) return -127;
  if (value > 127) return 127;
  return static_cast<int8_t>(value);
}

}  // namespace

uint8_t crc8(const uint8_t *data, std::size_t length) {
  uint8_t crc = 0;
  while (length-- > 0) {
    crc ^= *data++;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0xD5)
                         : static_cast<uint8_t>(crc << 1);
    }
  }
  return crc;
}

void decodeRcChannels(const uint8_t *payload, RcChannels &channels) {
  for (std::size_t channel = 0; channel < kChannelCount; ++channel) {
    const std::size_t bitIndex = channel * 11;
    const std::size_t byteIndex = bitIndex / 8;
    const uint8_t bitOffset = static_cast<uint8_t>(bitIndex % 8);

    uint32_t window = payload[byteIndex];
    if (byteIndex + 1 < kRcPayloadLength) {
      window |= static_cast<uint32_t>(payload[byteIndex + 1]) << 8;
    }
    if (byteIndex + 2 < kRcPayloadLength) {
      window |= static_cast<uint32_t>(payload[byteIndex + 2]) << 16;
    }
    channels.values[channel] = static_cast<uint16_t>((window >> bitOffset) & 0x7FF);
  }
}

int8_t mapAxis(uint16_t channel) {
  if (channel <= kChannelMin) return -127;
  if (channel >= kChannelMax) return 127;
  if (channel == kChannelCenter) return 0;

  if (channel < kChannelCenter) {
    const int32_t offset = static_cast<int32_t>(channel) - kChannelCenter;
    return clampAxis((offset * 127) / (kChannelCenter - kChannelMin));
  }
  const int32_t offset = static_cast<int32_t>(channel) - kChannelCenter;
  return clampAxis((offset * 127) / (kChannelMax - kChannelCenter));
}

int8_t mapThrottle(uint16_t channel) {
  if (channel <= kChannelMin) return -127;
  if (channel >= kChannelMax) return 127;
  const int32_t scaled =
      (static_cast<int32_t>(channel - kChannelMin) * 254) /
      (kChannelMax - kChannelMin);
  return clampAxis(scaled - 127);
}

HidReport makeHidReport(const RcChannels &channels) {
  return {
      mapAxis(channels.values[0]),
      mapAxis(channels.values[1]),
      mapThrottle(channels.values[2]),
      mapAxis(channels.values[3]),
      0,
      0,
      kHidHatCenter,
      0,
  };
}

HidReport makeFailsafeReport() {
  RcChannels safe{};
  for (auto &value : safe.values) value = kChannelCenter;
  safe.values[2] = kChannelMin;
  return makeHidReport(safe);
}

CrsfParser::CrsfParser(uint32_t interByteTimeoutUs)
    : state_(State::WaitSync),
      expectedLength_(0),
      index_(0),
      body_{},
      interByteTimeoutUs_(interByteTimeoutUs),
      lastByteUs_(0) {}

void CrsfParser::reset() {
  state_ = State::WaitSync;
  expectedLength_ = 0;
  index_ = 0;
}

bool CrsfParser::push(uint8_t byte, uint32_t nowUs, RcChannels &channels) {
  if (state_ != State::WaitSync &&
      static_cast<uint32_t>(nowUs - lastByteUs_) > interByteTimeoutUs_) {
    reset();
  }
  lastByteUs_ = nowUs;

  switch (state_) {
    case State::WaitSync:
      if (byte == kSyncByte) state_ = State::WaitLength;
      return false;

    case State::WaitLength:
      if (byte < 2 || byte > kMaxFrameBodyLength) {
        reset();
        return false;
      }
      expectedLength_ = byte;
      index_ = 0;
      state_ = State::ReadBody;
      return false;

    case State::ReadBody:
      body_[index_++] = byte;
      if (index_ < expectedLength_) return false;

      const bool crcValid =
          crc8(body_, expectedLength_ - 1) == body_[expectedLength_ - 1];
      const bool isRcFrame =
          body_[0] == kRcChannelsPackedType &&
          expectedLength_ >= kRcPayloadLength + 2;
      if (crcValid && isRcFrame) decodeRcChannels(&body_[1], channels);
      reset();
      return crcValid && isRcFrame;
  }
  reset();
  return false;
}

RcLinkState::RcLinkState(uint32_t timeoutMs, uint32_t retryIntervalMs)
    : timeoutMs_(timeoutMs),
      retryIntervalMs_(retryIntervalMs),
      lastFrameMs_(0),
      lastAttemptMs_(0),
      hasFrame_(false),
      hasAttempted_(false),
      failsafeSent_(false) {}

void RcLinkState::noteFrame(uint32_t nowMs) {
  lastFrameMs_ = nowMs;
  hasFrame_ = true;
  hasAttempted_ = false;
  failsafeSent_ = false;
}

bool RcLinkState::shouldSendFailsafe(uint32_t nowMs) {
  if (!hasFrame_ || failsafeSent_) return false;
  if (static_cast<uint32_t>(nowMs - lastFrameMs_) < timeoutMs_) return false;
  if (hasAttempted_ &&
      static_cast<uint32_t>(nowMs - lastAttemptMs_) < retryIntervalMs_) {
    return false;
  }
  lastAttemptMs_ = nowMs;
  hasAttempted_ = true;
  return true;
}

void RcLinkState::confirmFailsafeSent() {
  failsafeSent_ = true;
}

}  // namespace elrs

