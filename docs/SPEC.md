# How it works

The design of FPVGateC5RX: what it does and how the parts fit together. For
setup, see [GETTING_STARTED.md](GETTING_STARTED.md).

## What it does

To an FPVGate timer, the C5 looks exactly like an RX5808 receiver module:

1. It accepts RX5808 tuning commands from an unmodified FPVGate.
2. It tunes its own 5 GHz radio to the requested FPV channel.
3. It outputs the signal strength (RSSI) as a voltage on the RSSI pin, as an
   RX5808 does. A host can also read it digitally over the bus.

It doesn't output video or transmit. It can't run Wi-Fi while receiving either,
because the C5 has only one radio.

## Overview

```
 FPVGate (unchanged)                 ESP32-C5 (this firmware)
 -------------------                 ------------------------------------------
 RX5808 driver --SEL/CLK/DATA----->  bus receiver (GPIO interrupts)
                                         |  25-bit words
                                         v
                                     controller ----> radio: tune the channel,
                                         |                   read the RSSI (1 kHz)
                                         v
                                     RSSI pipeline: peak-hold, median, smoothing,
                                         |          soft ceiling, 0 to 255
                                         v
 ADC <---RSSI--- RC filter <--- GPIO10 sigma-delta output
 bus read <------------------------- RSSI registers (digital)
```

The code in `firmware/core/` is plain C++ with no hardware dependencies, and
the unit tests run it on a PC (`bash test/run.sh`). `firmware/src/` holds the
C5-specific parts: interrupts, radio, sigma-delta output, saved settings and
the serial console.

## The RX5808 bus

FPVGate controls the bus and the C5 responds.

- **SEL** goes low for the length of a frame. FPVGate drives **CLK**.
  **DATA** carries data in both directions.
- A frame is 25 bits, least significant first: 4 address bits, 1 R/W bit
  (1 = write), then 20 data bits.
- **Writes:** the C5 samples DATA on each rising edge of CLK.
- **Reads:** after the first 5 bits, the C5 drives DATA with the register's
  value. It changes the bit on each falling edge of CLK, so it's steady when
  FPVGate samples it with CLK low.
- FPVGate clocks writes at about 300 us per phase, but its frequency read-back
  at about 10 us. So the interrupt handlers use Espressif's direct register
  functions (`gpio_ll_*`); Arduino's `pinMode` and `digitalRead` aren't safe in
  an interrupt on this core, and are too slow.

| Register | On a write | On a read |
|---|---|---|
| 0x1, frequency | Work out the MHz and retune | Returns the last value written, so FPVGate's `verifyFrequency()` matches |
| 0xA, power | All ones powers down; anything else powers up | |
| 0xF, reset | Restart the radio and retune | |
| 0x6, info (extra) | | D0-7 signal strength in dBm (signed); D8-15 signature 0xC5 |
| 0x7, status (extra) | | D0-7 RSSI (0 to 255); D8 valid; D9 channel supported; D11-14 state; D15 always 1 |

**Working out the frequency.** FPVGate calculates `tf = (f - 479) / 2`,
`N = tf / 32`, `A = tf % 32` and sends `reg = (N << 7) + A`. The C5 reverses
that: `f = ((reg >> 7) * 32 + (reg & 0x7F)) * 2 + 479`. The RX5808 tunes in
2 MHz steps, so the result can be 1 MHz off the intended channel (R1, 5658 MHz,
comes back as 5657). The C5 snaps it to the nearest standard channel within
2 MHz.

## The radio

- **Start-up:** Wi-Fi in station mode, never connecting or scanning; 5 GHz
  only; a country setting that allows every 5 GHz channel the C5 supports (it
  only receives, so it never transmits on them); and promiscuous mode, so the
  receiver runs all the time.
- **Tuning:** the requested frequency goes to the nearest 5 GHz Wi-Fi channel
  (36-64, 100-144 or 149-177), up to 12 MHz away. That puts the FPV signal
  inside the receiver's 20 MHz bandwidth. Channels with no Wi-Fi channel close
  enough (R8, E6-E8, L1-L4) are reported as unsupported: no signal, but no
  error.
- **Reading the signal:** Espressif's `phy_get_rssi()` gives the noise floor
  plus the gain the AGC chose, in dBm. With the AGC on automatic it follows an
  analog FPV signal. It updates about every 25 ms, and each update shows a 2 to
  3 ms dip of about 10 dB, which the pipeline's peak-hold removes. The firmware
  reads it every millisecond.

## The RSSI pipeline

From a reading in dBm to FPVGate's 0 to 255, in `core/rssi_pipeline.*`:

1. **Peak-hold (30 ms):** outputs the highest reading of the last 30 ms, which
   hides the regular dips. A rise comes through immediately; a fall is delayed
   by up to 30 ms.
2. **Median of three:** drops single-reading glitches.
3. **Smoothing:** off by default. Any smoothing visibly rounded the edges in
   testing, and FPVGate does its own filtering.
4. **Soft ceiling (optional):** above a chosen level, extra signal is
   compressed, so strong signals bunch together but still peak, rather like an
   RX5808 that saturates.
5. **Calibration:** `dbLo` reads as 0 and `dbHi` as 255; anything outside is
   clamped. `dbHi` still reads 255 with the soft ceiling on.
6. **Settling and stalls:** for 35 ms after a retune the output holds its last
   value and is marked invalid, as FPVGate expects from an RX5808. If no new
   reading arrives for 200 ms, it's marked stalled.

The defaults are `dbLo -90`, `dbHi -20`, no smoothing and no soft ceiling. Our
bench setup used `cal -80 -15` and `knee -55 6`.

## The outputs

**Analog.** The C5 has no DAC. Its sigma-delta output switches GPIO10 on and
off four million times a second, and the share of time it's on sets the average
voltage. An RC filter turns that into a steady level. With the recommended
10k/10k divider, RSSI 255 is about 1.5 V, just under FPVGate's ADC limit. See
[HARDWARE.md](HARDWARE.md).

**Digital.** Registers 0x6 and 0x7 (see the table above). A host can check the
0xC5 signature and read the RSSI directly. FPVGate doesn't use this today.

## Settings and console

Calibration, smoothing, soft ceiling and boot frequency are saved in flash as a
block of bytes with a magic number, a version (currently 4) and a CRC32.
Damaged data, or data from another version, is ignored and the defaults are
used. The serial console ([CONSOLE.md](CONSOLE.md)) reads and changes all of
it.

## Pins

| | CLK | DATA | SEL | RSSI out |
|---|---|---|---|---|
| Default and C5-Zero | GPIO4 | GPIO5 | GPIO6 | GPIO10 |

These match the GPIO numbers FPVGate uses on a XIAO S3. They avoid the C5's
strapping pins (2, 3, 7, 8, 9, 25-28), UART0 (11, 12) and USB (13, 14). Change
them per board in `platformio.ini`.
