# Serial console

The C5 has a text console over USB at **115200 baud**, with lines ending in
newline (or CR). It answers on both the C5's built-in USB and its UART0, so it
works whichever way a board wires its USB-C connector.

Type `help` on the board for a short list. Changes take effect immediately,
but are lost at power-off unless you `save`.

```powershell
pio device monitor -b 115200 -p COM29          # interactive
python tools\c5cmd.py COM29 "ch R4" "s"        # scripted, and doesn't reset the board
```

## Everyday commands

| Command | What it does | Example |
|---|---|---|
| `s` or `status` | One status line (see below) | `s` |
| `ch <band><n>` | Tune to a channel | `ch R4`, `ch F2` |
| `tune <MHz>` or `f <MHz>` | Tune to a frequency, 4900 to 6000 (5180 to 5885 with `rf method wifi`). Nothing above about 5960 is usable. | `tune 5800` |
| `scan [ms]` | Measure every channel the C5 can receive, print each, then name the strongest. `ms` is the time per channel (default 60). Any other command stops it. | `scan`, `scan 100` |
| `sweep <lo> <hi> <step> [ms]` | Like `scan`, but every `step` MHz from `lo` to `hi`. Frequencies the radio can't tune are skipped. | `sweep 5870 5960 2 100` |
| `rf` | Radio settings, for experiments. Not saved: every boot starts with the defaults shown in brackets. See EXTENDED_TUNING.md. | `rf` |
| `rf method auto\|wifi\|phy` | How to tune: Wi-Fi channel when within 5 MHz of one, else direct (auto); Wi-Fi channel only; always direct | `rf method phy` |
| `rf 11p auto\|on\|off [0\|1]` | 802.11p mode (auto: on from 5750 MHz) | `rf 11p off` |
| `rf read sig\|rssi\|nf` | What the RSSI is made from: the live signal-RSSI register (sig), the old `phy_get_rssi` that freezes when nothing is detected (rssi), or the noise-floor estimate (nf) | `rf read sig` |
| `rf sigen 0\|1` | Signal-RSSI mode on (1, set at boot) or off | `rf sigen 1` |
| `rf post none\|twice\|track` | What runs after a direct tune (track) | `rf post track` |
| `rf hold on\|off` | Put the radio back if anything moves it (off) | `rf hold on` |
| `rf anchor auto\|<channel>` | Which Wi-Fi channel the driver sits on during a direct tune (auto: nearest) | `rf anchor 177` |
| `stream on [hz]` or `stream off` | Print the status line continuously (default 5 times a second, up to 50) | `stream on 10` |

The status line looks like this:

```
f=5769 R4 st=TRACKING db=-25.5 rssi=214 valid=1
```

| Field | Meaning |
|---|---|
| `f=` | The frequency it's tuned to, and the channel name |
| `st=` | `TRACKING` is normal. `TUNING` means it has just retuned, `IDLE` that nothing is tuned yet. You may also see `POWERDOWN` or `RF_FAULT`. |
| `db=` | Signal strength in dBm |
| `rssi=` | The 0 to 255 value sent to FPVGate |
| `valid=` | 0 while settling after a retune, or on a channel the C5 can't receive |

## Calibration and shaping

| Command | What it does | Example |
|---|---|---|
| `cal` | Show the calibration and shaping settings | `cal` |
| `cal lo` or `cal hi` | Use the current reading as the bottom or top of the scale | With the VTX off: `cal lo` |
| `cal <lo> <hi>` | Set the levels, in dBm, that read as 0 and 255 | `cal -80 -15` |
| `knee <dB> <ratio>` or `knee off` | Soft ceiling. Above `<dB>`, every `<ratio>` dB of extra signal only counts as 1 dB, so strong signals bunch together but still peak. | `knee -55 6` |
| `ema <alpha>` | Smoothing, from 0.01 to 1. **1 means off, the default.** Lower values smooth more but round off the edges. | `ema 1` |
| `premin [n]` | Lowest of the last `n` readings, before the peak-hold. Removes Wi-Fi bursts shorter than `n` ms. Default 8; 1 = off. Not saved. | `premin 8` |
| `hold [ms]` | Peak-hold window, which bridges the VTX's short dips. Default 30; 0 = off. Not saved. | `hold 40` |
| `raw [n]` | Print the next `n` raw readings (one per ms, dBm, before any filtering), up to 2000 | `raw 2000` |
| `boot <MHz>` or `boot off` | Frequency to tune at power-up. FPVGate's commands still override it. | `boot 5769` |
| `save` | Store all of the above | `save` |
| `defaults` | Go back to the default settings (not stored until you `save`) | `defaults` |

The 30 ms peak-hold that removes the C5's regular dips is always on.

## Connection to FPVGate

| Command | What it does |
|---|---|
| `bus` | What has arrived from FPVGate: frames, writes, reads, any frames that ended early, the last command and the frequency it works out to, plus the current SEL, CLK and DATA levels. Also the RSSI pin: how often GPIO10 reads high (about the output level, so `out 0` gives 0%), how often the sigma-delta clock has been found off and restarted, and the raw clock and routing registers. |

With FPVGate idle, it should show `SEL=...(1) CLK=...(0)`. When FPVGate tunes
the C5, the console also prints `host: tune <MHz> ...`.

## Troubleshooting commands

| Command | What it does | Example |
|---|---|---|
| `selftest` | Run the software self-test again. `0x1F PASS` means all is well. | `selftest` |
| `diag` | Radio status: how Wi-Fi started up, the channel in use, the live reading | `diag` |
| `out <0-255>` or `out off` | Fix the analog output at a set RSSI, to test the filter and wiring. 255 gives about 1.5 V at the RSSI junction. Remember `out off` afterwards. | `out 255` |
