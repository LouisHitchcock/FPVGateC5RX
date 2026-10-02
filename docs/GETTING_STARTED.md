# Getting started

From a bare ESP32-C5 board to a working FPVGate receiver. It takes about half
an hour.

The C5 replaces the RX5808 module on an FPVGate timer. FPVGate tunes it over
the same three wires it already uses, and the C5 sends the signal strength back
on the RSSI pin, just as an RX5808 does. FPVGate itself needs no changes.

## 1. What you need

**Hardware**

| Item | Notes |
|---|---|
| ESP32-C5 board | The **Waveshare ESP32-C5-Zero** is tested. Other ESP32-C5 boards should work with the generic build; check their pins first. |
| USB-C cable | For flashing and the serial console |
| Antenna | The C5-Zero's onboard antenna works. An external 5.8 GHz antenna may give more range. |
| 2 x 10k resistors and a 100 nF capacitor | The RSSI filter (essential) |
| 330 ohm resistor | For the DATA line (recommended) |
| Wire | Six connections to the FPVGate board |

A multimeter helps with troubleshooting, but you can manage without one.

**Software**

- [PlatformIO](https://platformio.org/), either the VS Code extension or
  `pip install platformio`.
- Git.

> **Windows:** run PlatformIO from **PowerShell**, not Git Bash. Under Git
> Bash, PlatformIO fails to install its flashing tool.

## 2. Get the code

```powershell
git clone https://github.com/LouisHitchcock/FPVGateC5RX.git
cd FPVGateC5RX\firmware
```

## 3. Choose a build

| Build | For |
|---|---|
| `waveshare_c5_zero` | The Waveshare ESP32-C5-Zero |
| `esp32c5` | Any other ESP32-C5 board. Check that its pins are free first (see [HARDWARE.md](HARDWARE.md)). |

## 4. Flash it

1. Plug the C5 into your PC.
2. Find its COM port. On Windows, look in Device Manager under *Ports*; it
   shows as "USB Serial Device".
3. Close any serial monitor that might have the port open.
4. Build and flash. The first build downloads the tools and takes a few
   minutes.

```powershell
pio run -e waveshare_c5_zero -t upload --upload-port COM29
```

Use your own COM port number. It's done when you see `Hash of data verified`
and `[SUCCESS]`.

**If the upload fails** with "could not open port", something else has the port
open: close it and try again. If the board doesn't respond at all, hold the
**BOOT** button while you plug it in, then flash again.

## 5. Check it's running

Open a serial monitor at **115200 baud**, with lines ending in newline:

```powershell
pio device monitor -b 115200 -p COM29
```

Or use the script in `tools`, which unlike most serial monitors doesn't restart
the board when it connects:

```powershell
python ..\tools\c5cmd.py COM29 "s"
```

At start-up the C5 prints:

```
FPVGateC5RX boot
selftest: 0x1F PASS
analog out: GPIO10 running
radio: ready
type 'help' for commands
```

Check for `selftest: 0x1F PASS`, `analog out: GPIO10 running` and
`radio: ready`.

## 6. Test the radio on its own

You can check the radio with only the board, a USB cable and a VTX; no wiring
needed.

1. Put an analog VTX on a known channel, for example **R4**, about 50 cm from
   the C5.
2. In the console, type:

```
ch R4
scan
```

`scan` measures every channel and names the strongest. It should be your VTX's
channel, well above the rest.

3. To watch it live, type:

```
stream on
```

Move the VTX closer and further away, and `db=` should follow it: roughly
-15 dBm close up, -60 dBm a few metres away, and -75 to -95 dBm with the VTX
off. Type `stream off` to stop.

Every command is listed in [CONSOLE.md](CONSOLE.md).

## 7. Wire it to FPVGate

**Take the RX5808 off first.** If both are connected, they fight over the RSSI
and DATA lines.

| FPVGate (XIAO S3) | C5-Zero | |
|---|---|---|
| D3 / GPIO4 (CLK) | GPIO4 | |
| D4 / GPIO5 (DATA) | GPIO5 | through a 330 ohm resistor |
| D5 / GPIO6 (SEL) | GPIO6 | |
| D2 / GPIO3 (RSSI) | the RSSI junction, below | |
| GND | GND | essential |
| 3V3 | 3V3 | or power each board over USB with the grounds joined |

**The RSSI filter.** This part is essential. The C5 has no DAC: it makes the
RSSI voltage by switching very fast, and this filter smooths that into a steady
level.

```
  C5 GPIO10 ---[ 10k ]---+----------- XIAO D2 (RSSI)
                         |
                         +---[ 10k ]---- GND
                         |
                         +---[ 100nF ]-- GND
```

The capacitor and the second 10k both go from the junction to ground. Only the
first 10k sits between GPIO10 and D2. [HARDWARE.md](HARDWARE.md) explains each
part.

## 8. Set up FPVGate

1. Leave **Receiver Module** set to **RX5808**.
2. Choose your channel as usual. The C5 retunes to whatever FPVGate selects.
   In the console, `bus` shows the commands arriving and `s` shows the
   frequency the C5 is on.
3. Calibrate the C5 (next step), then run FPVGate's calibration wizard.

![Three detections in FPVGate's graph, with the C5 on R4](images/rssi-gate-passes.png)

*FPVGate detecting three passes (shaded green), using the C5 as its receiver.*

## 9. Calibrate the C5

Calibration sets which signal levels count as the bottom and top of FPVGate's
scale. Do it once for each setup, in the console:

1. With the VTX **off**: `cal lo`
2. With the VTX **on** and held at gate-pass distance: `cal hi`
3. `save`

You can also set the levels directly, for example `cal -80 -15` (the dBm values
for 0 and for 255). Setting the top a few dB above your gate-pass level leaves
room for close passes.

**Optional soft ceiling:** `knee -55 6` makes strong signals bunch together,
rather like an RX5808 that saturates, while still giving a peak. `knee off`
removes it. Then `save`.

## 10. Day to day

- At power-on the C5 tunes its saved boot frequency, then follows FPVGate.
- Saved settings survive power cycles.
- To use the C5 without FPVGate, `boot <MHz>` then `save` makes it tune on its
  own at power-on.

## 11. Channels

| Band | Works? |
|---|---|
| Raceband R1-R7 | Yes |
| Raceband R8 (5917 MHz) | No, it's outside the C5's range |
| Bands A, B and F | Yes, all channels |
| Band E, E1-E5 | Yes |
| Band E, E6-E8 (5905, 5925, 5945 MHz) | No, outside the C5's range |
| Band L, L5-L8 | Should work, untested |
| Band L, L1-L4 | No, there's no Wi-Fi channel close enough |

On a channel it can't receive, the C5 accepts FPVGate's command but reports no
signal.

## 12. Troubleshooting

| Problem | Likely cause | What to do |
|---|---|---|
| No serial output | Wrong port, or the board is in download mode | Check Device Manager and press RESET |
| Upload says "could not open port" | The port is in use | Close any serial monitor and try again |
| FPVGate's RSSI stays at zero | The filter is wired wrong, the ground is missing, or it's on the wrong pin | Type `out 255`: the RSSI junction should read about 1.5 V. Check the capacitor runs from the junction to ground and the ground wire is fitted. Type `out off` afterwards. |
| FPVGate's RSSI jumps randomly and ignores the VTX | The filter is missing, or GPIO10 isn't working | Fit the filter. Check the C5 printed `analog out: GPIO10 running`. |
| The C5 doesn't follow FPVGate's channel | Bus wiring | Type `bus`. `frames=0` means nothing is arriving. Idle should show `SEL=...(1) CLK=...(0)`; if they're the other way round, SEL and CLK are swapped. |
| RSSI is stuck at 255 | The top of the calibration is too low | Raise it with `cal <lo> <hi>` |
| The background jumps about with the VTX off | Wi-Fi or other 5 GHz traffic on that channel | With the VTX off, type `cal lo` to raise the bottom of the scale |
| The RSSI rises in two steps when the VTX powers up | The VTX starts at low power, then switches to full power | That's the VTX, not the C5. `knee -55 6` makes it less visible. |

## 13. Updating

Flash the new build the same way (step 4). Your settings are kept unless an
update changes how they're stored. In that case the board starts on its
defaults, so check `cal` and `boot`, set them again and `save`.
