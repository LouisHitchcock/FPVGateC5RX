# Provenance

This firmware was written from scratch, without using code from the GPL
projects that also use the ESP32-C5's radio for FPV. This page records where
each technical fact in it came from, so that can be checked. The reasoning is
in [LICENSING.md](LICENSING.md).

## The rules followed

**Allowed:**

- Espressif's own material: Apache-2.0 headers and libraries (including
  studying how the PHY library works), the ESP32-C5 datasheet and technical
  reference, and the Arduino-ESP32 and ESP-IDF APIs.
- Public facts, such as "the C5 receives 5180 to 5885 MHz". Facts, methods and
  ideas aren't protected by copyright.
- The RX5808 register protocol, from the RX5808 datasheet and FPVGate's own
  RX5808 driver (the same author's code).
- Measurements taken on real hardware.

**Not allowed:**

- Source code from esp-sdr, C5VRX or double-ESP-resso: not read, not copied,
  not translated, not used as a model. During development only their public
  READMEs, documentation and one issue discussion were read, never their
  source.

## Facts used in the firmware

| Fact | Where it came from |
|---|---|
| RX5808 bus format: 25 bits, least significant first; 4 address bits, 1 R/W bit, 20 data bits; frequency register 0x1, with `tf = (f - 479) / 2`, `N = tf / 32`, `A = tf % 32`, `reg = (N << 7) + A` | RX5808 datasheet; FPVGate `lib/RX5808/RX5808.cpp` |
| FPVGate clocks writes at about 300 us per phase and the frequency read-back at about 10 us, and waits 35 ms after tuning | FPVGate `lib/RX5808/RX5808.h` and `.cpp` |
| The C5 receives 5180 to 5885 MHz and has a single radio | ESP32-C5 datasheet |
| The C5's 5 GHz channels are 36-64, 100-144 and 149-177 | Espressif `esp_wifi_types_generic.h` (Apache-2.0) |
| How to start Wi-Fi in 5 GHz-only mode with every channel allowed | Espressif `esp_wifi.h` API documentation (Apache-2.0) |
| **How the firmware reads the signal:** `phy_get_rssi()`, with the AGC on automatic, follows an analog FPV signal; the noise-floor register doesn't | **Our own bench tests** (a VTX switched on and off, readings compared). What the function returns: Espressif's PHY library (Apache-2.0). |
| `phy_11p_set(1, 0)` (802.11p mode) improves reception from 5750 to 5990 MHz, about 14% less noise on R8 | A public tip on X (@Ready4Sushi, 2026-10-01; a fact, no code. Its chart compares against esp-sdr, which is GPL: none of its code was looked at), confirmed by **our own bench test** on R8. The function's arguments: Espressif's PHY library (Apache-2.0). |
| `phy_get_rssi()` only updates on receive events; `phy_check_sigrssi_en(1)` + `phy_get_sigrssi()` measure continuously | **Our own reading** of Espressif's PHY library (Apache-2.0), confirmed by **our own bench tests** (VTX on/off on R8 and R4). |
| Above channel 14 the PHY takes the frequency in MHz, and `RFChannelSel(chan, bw)` tunes it to any MHz value | **Our own reading** of Espressif's PHY library (Apache-2.0): `phy_chan_to_freq`, `RFChannelSel`, `phy_chip_set_chan`. See EXTENDED_TUNING.md. |
| The reading refreshes about every 25 ms, with a 2 to 3 ms dip of about 10 dB; a 30 ms peak-hold removes it | **Our own bench tests** (1 ms captures) |
| The sigma-delta output, for an analog voltage without a DAC | Espressif `soc_caps.h` and the `driver/sdm.h` API (Apache-2.0) |
| Interrupt-safe pin access with `gpio_ll_*` | Espressif `hal/gpio_ll.h` (Apache-2.0) |
| The Arduino core only creates `HWCDCSerial` with USB-CDC-on-boot | Arduino-ESP32 core headers (a fact about its API) |
| Waveshare C5-Zero pins: BOOT on GPIO9, antenna switch on GPIO26, RGB LED on GPIO27, strapping pins | A community ESPHome board definition (pin facts only); Espressif `io_mux_reg.h` |

## Explored and not used

These were investigated during development but are not in the firmware:

| Idea | Source | Outcome |
|---|---|---|
| Reading the chip's noise-floor register (`phy_read_hw_noisefloor`) as the signal level | Espressif's PHY library (Apache-2.0) | Didn't respond to a signal on real hardware. Dropped. |
| Freezing the AGC and forcing a fixed gain | Espressif's PHY library for the functions; the idea that it matters for analog signals came from a public C5VRX issue discussion | Not needed, because the AGC works on automatic. Removed. |
| Tuning with a fine frequency offset | Espressif's PHY library | Not needed: the nearest Wi-Fi channel is close enough. |
| Re-arming the AGC to refresh the reading faster | Espressif's PHY library | Made no difference. Removed. |

If anything is ever found to trace back to a GPL project's code, it will be
taken out and rewritten. Nothing currently does.
