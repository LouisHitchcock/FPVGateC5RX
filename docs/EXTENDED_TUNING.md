# Extended tuning: R8, E6-E8 and 802.11p mode

Status: **working on the bench, R8 end to end through FPVGate (2026-10-03).**
Started 2026-10-03.

## Bench results, 2026-10-03 (C5-Zero, VTX at about 1 m)

- **The default is now `rf method auto`.** It uses the Wi-Fi method wherever
  that works, and the phy method (`RFChannelSel` + `post track`) only for
  frequencies the driver can't reach. FPVGate tunes R8 over SPI with no
  console needed.
- **Nothing retunes the radio behind us.** The PHY stays on the frequency we
  set (`phy_param` + 288 watched every reading, zero drifts).
- **The real problem was the RSSI reading, not the tuning.**
  `phy_get_rssi()` = noise-floor estimate / 4 + an AGC gain byte (bits 8-15
  of `0x600A706C`) that only updates when the receiver detects something.
  With nothing to detect it holds its last value. In-band channels hide this
  because background Wi-Fi keeps refreshing it. At R8 it froze, which looked
  like a slow "settle" and a stuck-high baseline in FPVGate.
- **The fix is signal-RSSI mode.** `phy_check_sigrssi_en(1)` at start-up,
  then read `phy_get_sigrssi()` (the same byte, signed, now measured all the
  time). R8: about -48 dBm with the VTX on, -94 off, live from the first
  sample. R4 with no VTX: -95 floor, with spikes to -72..-80 from Wi-Fi
  packets. Now the default (`rf read sig`). Calibration set to `cal -92 -30`,
  knee off.
- **T2 passes:** a 4 MHz sweep peaks at 5912-5920 (centred on 5917), about
  45 dB above the floor. Above about 5960 MHz the reading falls to about -122.
- **R4 hops settle instantly** with either method, and with the driver parked
  on channel 177 or 36. Distance between driver and radio doesn't matter.
- **Still open:**
  - The 11p A/B (T3/T4) hasn't run. 11p auto is on from 5750 MHz.
  - Sig mode sees Wi-Fi packets on in-band channels. Check they can't trigger
    false laps.
  - The C5 restarted once during testing (cause unknown; power suspected),
    fell back to its boot frequency (R4) and FPVGate didn't re-send R8.
  - `rf` settings and the phy range still aren't saved, and `boot` still
    rejects >5885.
  - FPVGate enter/exit thresholds need re-setting for the new scale.

(Original plan follows.)

## Where this came from

A tip on X (SushiDude, @Ready4Sushi, 2026-10-01,
https://x.com/Ready4Sushi/status/2105743436833862082):

> if you enable phy_11p_set(int enable, int mode) phy_11p_set(1, 0) on the
> ESP32 C5 you get better reception from 5,75-5,99 GHz

The attached chart plots "reception margin" from 0 to 6 GHz for "stock
esp-sdr" against "Wi-Fi driver tuning (ITS-G5 trick)". ITS-G5 is the European
name for 802.11p, 5855-5925 MHz. Above about 5.75 GHz, esp-sdr falls to
roughly 10 dB of margin. The ITS-G5 line holds about 40 dB up to 6.0 GHz. A
second developer then confirmed on Discord that R8 works, with about 14% less
noise on R8 with 11p on.

esp-sdr is GPL. We used only the facts above (a function name, its
arguments, a frequency range), not esp-sdr's code. See PROVENANCE.md.

## What we found in libphy

From Espressif's Apache-2.0 `libphy.a` for the C5 (arduino-esp32 3.3.5):

1. **The PHY tunes by MHz, not by channel number.** `phy_chan_to_freq(ch)`
   returns `ch` unchanged for anything above 14. In other words, a 5 GHz
   "channel" inside the PHY is the frequency in MHz. The 36-177 channel list
   is a Wi-Fi driver limit, not a radio limit.
2. **`RFChannelSel(int chan, int bw)`** is exported (in `phy_api.o`). It
   calls `phy_chip_set_chan(chan, bw)`, the same full channel change the
   driver uses: PLL, gain tables, DC calibration and AGC. It stores the
   frequency in the PHY's state, so the PHY's own housekeeping re-uses it.
3. **The PLL maths has no upper limit.** `phy_rfpll_set_freq` is plain
   fractional-N arithmetic on the MHz value.
4. **Calibration is per sub-band**, chosen by `phy_freq_to_index`:
   up to 5240, 5320, 5560, 5640, 5755, 5835 MHz, and everything above 5835.
   R8 and E6-E8 share the top sub-band's calibration with R7 rather than
   overrunning a table. The 5755 boundary matches the "5.75" in the tip.
5. **`phy_11p_set(enable, mode)`** stores both values, writes baseband
   registers and some analog (I2C block 103) registers. `mode` picks between
   two register values. While `enable` is set, `phy_chip_set_chan`
   re-applies it on every channel change. Turning it off is a separate call.
   **Hypothesis to test:** 802.11p uses 10 MHz channels, so 11p mode may
   narrow the receive filter. That would explain less noise, and it would
   make off-centre tuning worse (see T4).

## What the firmware does

- `rf method auto` (default): the Wi-Fi channel method wherever it works,
  direct PHY tuning everywhere else.
- `rf method wifi`: the old way. Tunes the nearest Wi-Fi channel, so the
  carrier can be up to 12 MHz off centre. Range 5180-5885.
- `rf method phy`: tunes the nearest Wi-Fi channel, so the driver stays on
  5 GHz, then calls `RFChannelSel(mhz, 0)` to move the radio onto the exact
  frequency. Range 4900-6000 MHz, any MHz value.
- `rf post track` (default): one `phy_param_track_tot(1, 0)` calibration pass
  after a direct tune.
- `rf read sig` + `rf sigen 1` (default): RSSI from the live signal-RSSI
  register, not the freezing `phy_get_rssi()`.
- `rf 11p auto|on|off [0|1]`: auto turns 11p on from 5750 MHz. The number
  is the `mode` argument (default 0).
- `rf hold`, `rf anchor`: experiment switches (put the radio back if it
  moves; force the driver's channel).
- `sweep <lo> <hi> <step> [ms]`: like `scan`, but every `step` MHz.
- `diag` shows the method used for this channel, 11p, the driver's channel,
  the PHY's frequency, and all three readings.

`rf` settings are **not saved**. Every boot starts with the defaults above.

The experiments that led here: R8 read fine once the receiver had detected
something, but `phy_get_rssi()` froze in between. That looked like a
0.2-3.5 s "settle" after every tune, sweeps peaking in the wrong place,
and FPVGate's baseline stuck high. Hops on R4 were instant, even with the
driver parked 590 MHz away, so it wasn't the tuning. A frozen value with
the VTX off was what gave it away.

## Plan

**Phase 0, done:** the experiment build above.

**Phase 1, decide (needs T1-T8):**
- If T1, T2 and T6 pass, PHY tuning works. If T8 shows it is as good or
  better on every channel, make `phy` the default for every channel, not
  just R8. Exact centring should help everywhere.
- Set the 11p policy from T3 and T4: off, on from some frequency, or always.
- If T6 shows the driver pulling the radio back, add a re-tune guard: re-apply
  `RFChannelSel` when the reading collapses. Or wrap the call in
  `phy_enter_critical()` / `phy_exit_critical()` (from the esp_phy component).
- Store method and 11p in `PersistConfig` (bump the version) so they survive
  a reboot.

**Phase 2, widen the range everywhere:**
- Split `kC5MaxMhz` into "datasheet range" and "tunable range" (from the backend).
- `params.cpp` currently rejects `boot` above 5885, so R8 can't be the
  boot frequency yet. Validate against the tunable range instead.
- Selftest `ST_RANGE`, the channel tables in README / GETTING_STARTED / SPEC,
  and CONSOLE.md.
- Rewrite the tests that assert R8 is unsupported (test_backend, test_decode,
  test_params, test_controller, test_console) so they apply to the wifi method only.
- L1-L4 (5362-5621) are within PHY reach too. Test them the same way.

**Phase 3, FPVGate:** analog (Level 1) needs no FPVGate change. FPVGate
already sends R8 over the bus, and the C5 will now report it supported. Check
it in T9.

**Risks:**
- Undocumented libphy functions. A framework update could change them. Stay
  on arduino-esp32 3.3.5 until re-tested.
- The Wi-Fi driver doesn't know the radio moved. Anything that makes it
  re-tune (unlikely when idle in promiscuous mode) puts the radio back on
  the Wi-Fi channel. T6 checks this.
- `RFChannelSel` runs on our task, not the Wi-Fi task. T6 and T7 look for glitches.
- Receive only. Nothing is transmitted on any of these frequencies.

## Test procedures

**Kit:** C5-Zero with its antenna, USB console at 115200, a VTX that can do
R1-R8 (and E6-E8 if it has band E), at a fixed 1 m, at its lowest power. If
you have one, an RX5808 or another receiver for comparison. Keep everything
in the same place for the whole session, and log the console to a file.

Before every test: `rf method wifi`, `rf 11p auto`, `stream off`.

### T0. Smoke test
1. Flash, open the console, run `rf`, then `diag`.
2. **Pass:** `rf method=wifi (tunes 5180-5885 MHz) 11p=auto mode=0`. `diag`
   shows every driver call `ESP_OK`.

### T1. Does PHY tuning reach R8? (go/no-go)
1. VTX on R8. `rf method wifi`, `ch R8`. Expect the "outside the C5 range" error.
2. `rf method phy`, `rf 11p off`, `ch R8`, `stream on 10`. Record 10 s.
3. Switch the VTX off. Record 10 s. `stream off`.
4. `diag`: note `driver reports channel` (expect 177) and `tuned=5917`.
5. **Pass:** with the VTX on, at least 15 dB above VTX off.

### T2. Is the radio really where we say?
1. Still `rf method phy`, `rf 11p off`, VTX on R8.
2. `sweep 5870 5960 2 100`.
3. VTX on R7 (5880): `sweep 5840 5920 2 100`.
4. **Pass:** the peak is within 3 MHz of the VTX (5914-5920 for R8,
   5877-5883 for R7). **Fail mode:** every sweep peaks at the same place,
   or the readings don't change across the sweep. That means the PLL isn't
   moving, and the driver channel is all you're seeing.

### T3. 802.11p A/B (reproduce the 14%)
On R8, then R4 (5769), then R1 (5658), all with `rf method phy`. For each of
`rf 11p off`, `rf 11p on 0` and `rf 11p on 1`:
1. VTX off: `stream on 10` for 30 s. Record the mean and spread of `db`.
2. VTX on: 30 s. Record the mean.
3. Margin = on mean minus off mean.
4. **Pass for "11p helps" on a channel:** the margin is at least 1 dB more
   than with 11p off, or the off-spread is clearly smaller with the signal
   no lower. Ask the other developer what their "14%" measured, so the
   numbers compare.

### T4. Filter width: does 11p narrow it?
1. VTX on R5 (5806). `rf method phy`.
2. `rf 11p off`, `sweep 5766 5846 1 100`. Then `rf 11p on 0` and the same sweep.
3. For each, write down the frequencies 3 dB and 10 dB below the peak, on
   both sides.
4. **Result:** if 11p is clearly narrower, it must only be used with
   `method phy` (exact centring). With `method wifi` the carrier can be 12 MHz
   off centre. Also re-check `rf method wifi` with 11p on R3 (5732, 12 MHz
   off its channel), against 11p off.

### T5. Wide sweep (the tweet's chart)
1. `rf method phy`, `rf 11p off`, VTX off: `sweep 4900 6000 10 60`.
2. Same with `rf 11p on 0`.
3. VTX on R8: both again.
4. **Look for:** "no reading" anywhere, a noise floor that jumps at the
   calibration boundaries (5240, 5320, 5560, 5640, 5755, 5835) and where
   reception stops at the top. Send me the logs and I'll plot them.

### T6. Does it stay tuned?
1. `rf method phy`, `ch R8`, VTX on. `stream on 1` for 30 minutes.
2. `diag` at the start and end.
3. **Pass:** no drop to VTX-off levels and no reset. Any dip that lasts
   should be matched against a `diag` (is the driver channel still 177?).

### T7. Hopping and settle time
1. `rf method phy`, VTX on R8. `scan 20` five times.
2. `stream on 50`, then `ch R1` and `ch R8` back and forth by hand.
3. **Pass:** every scan shows R8 as the peak, no crashes, and readings are
   valid within the 35 ms settle time after each tune.

### T8. Is PHY tuning as good everywhere?
1. For each of R1 to R8: VTX on that channel, `ch Rn`, `stream on 10` for 10 s,
   with `rf method wifi` (R8 not possible) and then `rf method phy`. 11p off.
2. Record the mean `db` for each.
3. **Pass:** `phy` is within 1 dB of `wifi` or better on every channel.
   Expect a gain on R3 (12 MHz off its Wi-Fi channel).

### T9. FPVGate end to end
1. Wire the C5 to FPVGate as usual. Open the console. Set `rf method phy`
   (it isn't saved, so do this after every C5 reset).
2. In FPVGate set the node to R8.
3. `bus`: expect `host: tune 5917`. `diag`: `channel supported=1`.
4. Fly or walk a quad on R8 through the gate.
5. **Pass:** the RSSI graph in FPVGate shows the passes, and laps are counted.
