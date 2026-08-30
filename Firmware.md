# ELRS Dongle Firmware

The firmware converts CRSF RC data from an ExpressLRS receiver into a native
USB HID gamepad on an ESP32-S3.

## Repository layout

| Path | Purpose |
|---|---|
| `ELRS_Dongle.ino` | ESP32-S3 hardware setup and main loop. |
| `ELRSProtocol.h/.cpp` | Testable CRSF parser, channel decoder, scaling, and failsafe state. |
| `tests/test_elrs_protocol.cpp` | Host-side protocol and behavior regression tests. |
| `.github/workflows/firmware.yml` | Native tests and an ESP32 Arduino 3.3.4 build. |
| `Body.stl`, `Top.stl`, `Dongle Case-*.3mf` | Printable enclosure files. |

## Data flow

1. UART1 reads the receiver at 420000 baud.
2. `CrsfParser` collects a frame, rejects invalid lengths and CRCs, and resets
   a stalled partial frame after 2 ms.
3. Valid type `0x16` payloads are unpacked into sixteen 11-bit channels.
4. Channels 1-4 become X, Y, throttle/Z, and Rz USB axes.
5. If no valid RC frame arrives for 1000 ms after the link has been active, a
   safe report centers roll, pitch, and yaw and lowers throttle. A rejected USB
   send is retried every 20 ms until delivery succeeds.

## Wiring

| Receiver signal | ESP32-S3 | Notes |
|---|---|---|
| TX | GPIO 3 | Required CRSF data connection. |
| RX | GPIO 4 | Reserved for future telemetry; currently unused. |
| Power and ground | Board-appropriate supply and GND | Verify the receiver voltage before wiring. |

Change `kCrsfRxPin` and `kCrsfTxPin` in `ELRS_Dongle.ino` for a different
board layout.

## Arduino configuration

- Install ESP32 Arduino core 3.3.4.
- Select an ESP32-S3 board with native USB.
- Set **USB Mode** to **USB-OTG (TinyUSB)**. `ARDUINO_USB_MODE=0` is the
  TinyUSB/OTG mode used by `USBHIDGamepad`; Hardware CDC and JTAG mode cannot
  provide this HID interface.
- Compile and upload `ELRS_Dongle.ino` together with `ELRSProtocol.h` and
  `ELRSProtocol.cpp` in a folder named `ELRS_Dongle`.

The sketch deliberately stops at compile time when native USB is unavailable
or the incompatible hardware CDC/JTAG mode is selected.

## USB controls and calibration

| CRSF channel | HID control |
|---|---|
| CH1 | X / roll |
| CH2 | Y / pitch |
| CH3 | Z / throttle |
| CH4 | Rz / yaw |

The CRSF calibration constants are minimum `172`, center `992`, and maximum
`1811`. Directional axes are scaled separately below and above center so that
center is exactly zero and both endpoints reach the HID range. Throttle maps
minimum to `-127` and maximum to `127`. Inputs outside the calibrated range
are clamped.

The unused hat is sent as `HAT_CENTER` (`0` in ESP32 core 3.3.4), and unused
buttons and axes remain released/centered.

## Link-loss behavior

CRSF receivers can stop transmitting RC channel frames during failsafe. The
firmware records each valid CRC-checked RC frame. Once a working stream has
been seen, a 1000 ms gap requests a safe USB report with centered directional
axes, minimum throttle, centered hat, and no buttons. Delivery is confirmed
only when the HID API accepts the report; a temporarily busy USB interface is
retried every 20 ms. A later valid frame automatically resumes normal reports
and rearms the timeout.

Change `kRcFailsafeTimeoutMs` if a different delay is required. Do not set it
below the expected radio update interval.

## Tests

On Windows with `g++` available:

```powershell
./tests/run_tests.ps1
```

The suite checks a hand-derived packed-channel fixture, CRC rejection,
calibrated axis endpoints, neutral HID state, one-shot link failsafe, and
parser recovery after an inter-byte timeout. GitHub Actions runs the same
tests and compiles the sketch for ESP32-S3 against core 3.3.4.

## Troubleshooting

- **No USB gamepad:** confirm the cable carries data and USB Mode is USB-OTG,
  then check that the native USB connector is being used.
- **No axis movement:** confirm the receiver is bound, set to CRSF output, and
  its TX pad is connected to GPIO 3 with a common ground.
- **Axes reversed:** reverse the corresponding transmitter output or negate
  that axis when constructing the HID report.
- **Reduced travel:** first verify transmitter output limits, then calibrate
  the controller in the simulator.

