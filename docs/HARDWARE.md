# Hardware

How to wire the C5 to an FPVGate board, and why each extra part is there.

Both the ESP32-C5 and FPVGate's ESP32-S3 work at 3.3 V, so they connect
directly with no level shifting. Never connect 5 V to a C5 pin.

## Parts

| Part | Value | What it's for | Needed? |
|---|---|---|---|
| R1 | 10k | RSSI filter and divider | Yes |
| R2 | 10k | RSSI divider | Yes |
| C1 | 100 nF (marked 104) | RSSI filter | Yes |
| R3 | 330 ohm | Protects the DATA line | Recommended |
| C2 | 100 nF | Supply decoupling | Recommended |
| Antenna | 5.8 GHz | Reception | Yes |

Ordinary 5% or 10% parts are fine, through-hole or SMD.

## Wiring

Take the RX5808 off the FPVGate board, then connect the C5 in its place:

| Signal | FPVGate (XIAO S3) | ESP32-C5 |
|---|---|---|
| CLK | D3 / GPIO4 | GPIO4 |
| DATA | D4 / GPIO5 | GPIO5, through R3 (330 ohm) |
| SEL | D5 / GPIO6 | GPIO6 |
| RSSI | D2 / GPIO3 | the junction of R1, R2 and C1 (below) |
| GND | GND | GND |
| 3V3 | 3V3 | 3V3, or power each board over USB with the grounds joined |

The C5's pin numbers match the XIAO's on purpose, so each wire goes to the same
number on both boards. To use other pins, change them in `platformio.ini`.

Pins to avoid on an ESP32-C5:

- GPIO2, 3, 7, 8, 9, 25, 26, 27 and 28: strapping pins, which decide the boot
  mode at power-on.
- GPIO11 and 12 (serial) and GPIO13 and 14 (USB).
- On the Waveshare C5-Zero, GPIO9 is also the BOOT button, GPIO26 the antenna
  switch and GPIO27 the RGB LED. Its free header pins are 0, 1, 3, 4, 5, 6 and
  10.

## The RSSI filter

```
  C5 GPIO10 ---[ R1 10k ]---+----------- RSSI (FPVGate D2)
                            |
                            +---[ R2 10k ]---- GND
                            |
                            +---[ C1 100nF ]-- GND
```

R1 runs from GPIO10 to the junction. R2 and C1 both run from the junction to
ground. The RSSI wire to FPVGate is taken from the junction.

**Why it's needed.** The C5 has no DAC, so it can't output a steady voltage
directly. Its sigma-delta output switches GPIO10 between 0 V and 3.3 V four
million times a second, and the share of time it's on sets the average voltage.
R1 and C1 smooth that switching into a steady level. Without them, FPVGate
samples the switching at random moments and the RSSI is meaningless noise.

**Why R2.** FPVGate expects RSSI to stay below about 1.55 V, which is as high
as an RX5808 goes. R1 and R2 halve the C5's output, so the top of the range is
about 1.5 V. Without R2 the voltage is doubled and FPVGate's RSSI hits its
ceiling early.

**How the values work out:**

- The divider ratio is R2 / (R1 + R2) = 10k / 20k = 0.5. That matches the
  firmware's default (`dividerRatio = 0.5`).
- The filter cutoff is 1 / (2 x pi x 5k x 100 nF), about 320 Hz. The 5k is R1
  and R2 in parallel. That's fast enough to follow a gate pass easily, and
  smooths the 4 MHz switching to well under a millivolt of ripple.
- FPVGate sees about 5k of source resistance, which suits the ESP32 ADC, and C1
  sits right at the pin for the ADC to sample from.

**If you build it differently,** work the ratio out from the resistance between
GPIO10 and the junction, and from the junction to ground. If the filter resistor
sits before a separate divider, it counts as part of the top resistor. Then set
`dividerRatio` to match, or the voltages won't line up.

## The DATA resistor (R3)

FPVGate sends on DATA when tuning, and the C5 sends on it when FPVGate reads
the frequency back. For a moment at each changeover both can drive the line.
330 ohm in series keeps the current safely low.

## Decoupling (C2)

Fit 100 nF across the C5's 3V3 and GND pins, close to the board. The radio
draws current in bursts, and a clean supply keeps the RSSI steady. If the C5
runs from FPVGate's 3V3, check that supply can cope; otherwise power the C5
separately and join the grounds.

## Antenna

The Waveshare C5-Zero works with its onboard antenna. An external 5.8 GHz
antenna on the IPEX connector may give more range. (The board's antenna switch
is on GPIO26, which the firmware doesn't set yet.) Keep the antenna away from
the FPVGate board and any 2.4 GHz radios.
