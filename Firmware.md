# ELRS Dongle Firmware Documentation

This document explains how the `ELRS_Dongle.ino` sketch turns CRSF RC data into a USB HID gamepad on an ESP32-S3 device. Use it as a reference when adapting the code for different hardware or simulators.

## Repository Layout

| Path                 | Purpose |
|----------------------|---------|
| `ELRS_Dongle.ino`    | Firmware that bridges CRSF to USB HID. |
| `Body.stl`, `Top.stl`, `Dongle Case-*.3mf` | Printable enclosure pieces. |
| `Dongle.jpg`         | Reference photo of the assembled dongle. |
| `LICENSE`            | Project license (MIT). |
| `README.md`          | Project overview. |

## Firmware Overview

The firmware is organized into the following logical sections (top to bottom).

1. **CRSF link configuration** (`CRSF_*` macros): pin assignments, UART number, and baud rate for the ELRS/Crossfire receiver.
2. **Protocol constants**: CRSF addresses, frame types, and payload sizes required for parsing RC channel frames.
3. **USB gamepad declaration**: a single global `USBHIDGamepad` instance created as soon as the sketch starts so it can be used inside the main loop.
4. **Parser state**: a state machine (`CrsfParseState`) that reconstructs CRSF frames byte-by-byte.
5. **CRC helper** (`crsfCrc8`): verifies frame integrity using the CRSF polynomial `0xD5`.
6. **Channel unpacking** (`crsfDecodeRcChannels`): converts the packed 22-byte RC frame into 16 raw 11-bit channel values.
7. **Channel mapping** (`mapChannelToAxisInt8`, `mapChannelToThrottleInt8`): maps CRSF values to the signed 8-bit range expected by the HID report.
8. **Frame handling** (`handleCrsfFrame`): maps selected channels to joystick axes and sends them via USB HID whenever a valid RC frame arrives.
9. **Byte processor** (`processCrsfByte`): feeds the parser state machine with bytes from the UART.
10. **Arduino `setup`/`loop`**: initializes hardware (UART + USB) and continuously reads CRSF data.

The data flow is therefore: `Serial1.read()` → `processCrsfByte` → `handleCrsfFrame` → `Gamepad.send()`.

## Hardware and Wiring

| Signal | ESP32-S3 pin constant | Notes |
|--------|----------------------|-------|
| CRSF RX | `CRSF_RX_PIN` (default `3`) | Connect to the **TX** pin of the ELRS/CRSF receiver. |
| CRSF TX | `CRSF_TX_PIN` (default `4`) | Optional. Required only if you plan to send telemetry back to the receiver. |
| UART    | `CRSF_UART_NUM` (default `1`) | Uses `Serial1`; `Serial0` stays free for logging if you enable it later. |

Change the pin macros at the top of `ELRS_Dongle.ino` to match your board. The UART baud rate stays fixed at `420000` because CRSF uses that speed.

## USB HID Behavior

- A single HID gamepad interface is exposed.
- Channel mapping (from `handleCrsfFrame`):
  - CH1 → `axisX` (roll)
  - CH2 → `axisY` (pitch)
  - CH3 → `axisZ` (throttle; uses the throttle-specific scaling)
  - CH4 → `axisRz` (yaw)
- RX, RY, buttons, and hat switch are currently unused (`0`), but you can reassign additional channels inside `handleCrsfFrame`.
- `Gamepad.send` is only called after a CRC-valid RC channel frame has been decoded, so USB traffic matches the radio update rate (typically 100–500 Hz depending on ELRS settings).

### Adapting the HID Mapping

1. Extend the `buttons` bitmask or `hat` variable for switches.
2. Map additional channels to `rx`/`ry` axes to support auxiliary controls.
3. If you need a throttle that idles at 0 instead of -127, change `mapChannelToThrottleInt8` accordingly.

## CRSF Parser Details

- **State Machine**: `processCrsfByte` waits for a valid CRSF address, then a length byte, then reads `length` bytes which include the type, payload, and CRC.
- **Buffering**: `crsfBuffer` is large enough (`64` bytes) for the biggest expected frame. `crsfIndex` tracks how many bytes have been stored.
- **CRC Check**: `crsfCrc8` validates the data before passing it to `handleCrsfFrame`. Invalid frames are silently dropped by returning to the `WAIT_ADDR` state.
- **Payload decoding**: `crsfDecodeRcChannels` walks through the 22-byte payload, reassembling each 11-bit channel via bit arithmetic. Each channel is returned in the raw `0–2047` range expected for CRSF.

## Setup and Usage

1. **Select USB mode**: In Arduino IDE `Tools → USB Mode`, pick an option that turns on native USB (e.g., `USB-OTG`). Without this, the HID gamepad will not enumerate.
2. **Install dependencies**:
   - ESP32 Arduino core ≥ 3.3.4.
   - `USB.h` and `USBHIDGamepad.h` are shipped with the ESP32 core; no extra libraries are required.
3. **Flash** `ELRS_Dongle.ino` to your ESP32-S3 board.
4. **Power** the ELRS receiver from the ESP32 and connect its UART to the configured pins.
5. **Verify** the device enumerates as a USB gamepad on your PC. You can use your OS joystick tester or a simulator calibration screen.

## Troubleshooting

- **No joystick detected**: Make sure the board is in a USB mode that exposes HID; double-check that `USB.begin()` is called and the board actually enumerates (some boards need a data-capable cable).
- **No movement in simulator**: Confirm the receiver is bound and sending CRSF data. You can temporarily add logging (e.g., `Serial.printf`) around `Gamepad.send` to see if frames arrive.
- **Inverted axes**: Swap channel assignments or flip the sign inside `mapChannelToAxisInt8`.
- **Different channel ordering**: Change the channel indices in `handleCrsfFrame` to match your transmitter output map.

## Extending the Firmware

- **Telemetry / CRSF TX**: Implement additional frame handlers and use `CrsfSerial.write` when you need to send data back to the receiver.
- **Advanced inputs**: Use more CRSF channels for buttons or hats to simulate radio switches.
- **Smoothing**: Add a moving-average filter before `Gamepad.send` for a softer feel in simulators.
- **Failsafe handling**: Detect stale frame timestamps (e.g., no frame for X ms) and zero the axes to avoid stuck inputs.

Refer back to `ELRS_Dongle.ino` when modifying behavior; the code is heavily commented, and each section mentioned here maps directly to contiguous blocks in the sketch for ease of navigation.
