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
- **Tuning, two ways (`rf method auto`, the default, picks per frequency):**
  - *Wi-Fi channel:* where it can, the requested frequency goes to the nearest
    5 GHz Wi-Fi channel (36-64, 100-144 or 149-177), up to 12 MHz away. That
    puts the FPV signal inside the receiver's 20 MHz bandwidth. This is how
    R1-R7, A, B, F and E1-E5 are tuned.
  - *Direct PHY:* inside Espressif's PHY library a 5 GHz "channel" is just the
    frequency in MHz, so `RFChannelSel(mhz, 0)` tunes any frequency. Anything
    the Wi-Fi driver can't reach (R8, E6-E8, L1-L4) is tuned this way: the
    driver is put on the nearest Wi-Fi channel first, then the radio is moved
    onto the exact frequency, then one calibration pass
    (`phy_param_track_tot`) runs. The PHY's own copy of the frequency is
    checked on every reading, to catch anything moving it back.
  - Useful up to about 5960 MHz; above that the reading falls away.
- **802.11p mode** (`phy_11p_set`) is on from 5750 MHz. It came from a public
  tip and hasn't been A/B tested here yet.
- **Reading the signal:** at start-up the firmware switches the PHY into
  signal-RSSI mode (`phy_check_sigrssi_en(1)`) and then reads
  `phy_get_sigrssi()`, in dBm. This measures all the time. The older
  `phy_get_rssi()` (noise floor plus an AGC gain byte) only updates when the
  receiver detects something, so with nothing to detect it freezes at its last
  value. Background Wi-Fi hid that on in-band channels; at R8 it left the RSSI
  stuck high after the VTX went off. Bench, R8 at 1 m: about -48 dBm with the
  VTX on, -94 off. On channels with Wi-Fi traffic, packets show as short
  spikes (to about -72). The firmware reads it every millisecond.

## The RSSI pipeline

From a reading in dBm to FPVGate's 0 to 255, in `core/rssi_pipeline.*`:

1. **Pre-minimum (8 readings):** the lowest of the last 8 readings (8 ms).
   This removes any upward burst shorter than 8 ms. On channels inside the
   Wi-Fi bands, background Wi-Fi shows as spikes of mostly 1 to 3 ms about
   every 24 ms, and step 2 would otherwise hold them continuously. A rise is
   delayed by 7 ms. `premin` changes it, 1 = off.
2. **Peak-hold (30 ms):** outputs the highest value of the last 30 ms. A VTX
   reading dips by 6 to 14 dB for 1 to 6 ms several times a second (made
   7 ms wider by step 1); this bridges them. A fall is delayed by up to 30 ms.
   `hold` changes it, 0 = off.
3. **Median of three:** drops single-reading glitches.
4. **Smoothing:** off by default. Any smoothing visibly rounded the edges in
   testing, and FPVGate does its own filtering.
5. **Soft ceiling (optional):** above a chosen level, extra signal is
   compressed, so strong signals bunch together but still peak, rather like an
   RX5808 that saturates.
6. **Calibration:** `dbLo` reads as 0 and `dbHi` as 255; anything outside is
   clamped. `dbHi` still reads 255 with the soft ceiling on.
7. **Settling and stalls:** for 35 ms after a retune the output holds its last
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

The sigma-delta's clock gate (`SDM.misc.sigmadelta_clk_en`) was sometimes
found off after start-up, typically after a USB reset, even though the driver
had turned it on. With no clock the output freezes, usually high, and FPVGate
reads about 241 whatever the signal. Only a power cycle cleared it. The
firmware now checks the gate on every output update and turns it back on.
`bus` shows how often that has happened, and how often GPIO10 actually reads
high.

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
