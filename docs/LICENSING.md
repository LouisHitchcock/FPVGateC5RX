# Licensing

This explains the licence and the reasoning behind it. It isn't legal advice.
If you plan commercial use, or rely on the reasoning below for a commercial
decision, have it checked by an IP solicitor.

## In short

| You want to... | Allowed? |
|---|---|
| Build it and use it yourself, at home or at a club | Yes, under CC BY-NC-SA 4.0 |
| Change it and share your changes | Yes. Credit the author and share under the same licence. |
| Use it at a free event | Yes |
| Sell hardware that runs it, sell the firmware, or use it in a paid product or service | Only with a commercial licence (see below) |

## The non-commercial licence

FPVGateC5RX is licensed under **Creative Commons
Attribution-NonCommercial-ShareAlike 4.0** (CC BY-NC-SA 4.0), the same licence
as FPVGate:

- **Attribution:** credit the author, link to the licence, and say if you
  changed anything.
- **NonCommercial:** no use "primarily intended for or directed towards
  commercial advantage or monetary compensation" (the licence's own words).
- **ShareAlike:** if you share a modified version, it has to be under the same
  licence.

Full terms: <https://creativecommons.org/licenses/by-nc-sa/4.0/legalcode>. A
summary is in [`LICENSE`](../LICENSE).

## Commercial licensing

Commercial use needs a separate licence from the copyright holder, Louis
Hitchcock. For example:

- selling timing hardware that uses this firmware;
- selling the firmware, or modules or kits with it loaded;
- running a paid service that uses it.

Terms are agreed case by case. To ask, email **louishitchcock@gmail.com**.

A commercial licence covers this project's own code. The third-party parts of
a firmware binary keep their own licences (see the last section), and a
commercial licensee still needs to meet those terms. They aren't onerous.

Not sure whether something counts as commercial, such as a club race that
charges entry to cover its costs? Ask.

## Why this isn't covered by the GPL

Other projects have shown that the ESP32-C5's radio can pick up 5.8 GHz FPV
signals: esp-sdr, C5VRX and double-ESP-resso. Their code is GPL, so it's fair
to ask whether this firmware has to be GPL too. It doesn't, for these reasons.

### What the GPL covers

The GPL is a copyright licence. Its conditions apply when you copy, change or
distribute GPL code, or a program based on it (including one that links it
in). Copyright protects the way code is written. It doesn't protect:

- ideas, methods or facts, such as "the C5's radio can measure an FPV signal"
  or "the C5 receives 5180 to 5885 MHz". (US: 17 U.S.C. 102(b). UK and EU:
  *SAS Institute v World Programming*, decided by the EU Court of Justice in
  2012 and the UK Court of Appeal in 2013, which held that what a program does,
  and the ideas behind it, are not protected.)
- separate code written independently to do a similar job.

So the question isn't whether this firmware does something similar. It's
whether it contains or is based on their code. It doesn't.

### What wasn't used

- No source code from esp-sdr, C5VRX or double-ESP-resso was copied,
  translated, adapted or used as a model. Their source was never read. Only
  their public READMEs, documentation and one public issue discussion were
  read, for general facts.
- The one technique taken from that issue discussion, freezing the radio's
  automatic gain control (AGC), isn't in the firmware. It was tried and then
  removed; the firmware leaves the AGC on automatic.
- None of those projects is a dependency. Nothing from them is linked into or
  shipped with this firmware.

### Where the knowledge came from instead

| Source | Status | Used for |
|---|---|---|
| Espressif's headers and libraries (ESP-IDF, the PHY and Wi-Fi libraries) | Apache-2.0, which allows use, study and commercial use | Register addresses, radio functions, understanding Espressif's own code |
| ESP32-C5 datasheet | Public documentation | Frequency range, the single radio, pin details |
| RX5808 datasheet and FPVGate's RX5808 driver | Datasheet; FPVGate is the same author's code | The bus protocol and frequency maths |
| Tests on a real ESP32-C5 | Original work | **How the shipped firmware reads the signal** |

The last row matters most. The first plan, reading the chip's noise-floor
register, **didn't work** on real hardware. The method that works was found on
our own bench: Espressif's per-signal RSSI with the AGC on automatic, plus a
30 ms peak-hold to remove the AGC's regular dips. We got there with diagnostic
commands written for the job, by switching a VTX on and off and comparing the
readings. That's an independent result, not something taken from another
project.

Each technical fact used, and where it came from, is listed in
[`PROVENANCE.md`](PROVENANCE.md), so the claim can be checked.

### What follows from that

Because the firmware isn't based on GPL code, its author can choose its
licence. That's why it can be non-commercial with commercial licences sold
separately, which a GPL-based project couldn't do.

## Third-party components

All of the source code in this repository is original. A compiled firmware
binary also contains other people's code, under their own licences:

| Component | Licence | What it means |
|---|---|---|
| ESP-IDF and Espressif's Wi-Fi and PHY libraries | Apache-2.0 | Permissive. Keep Espressif's licence and notices when you distribute a binary. |
| FreeRTOS, newlib and other parts of ESP-IDF | MIT and BSD-style | Permissive. Keep the notices. |
| Arduino-ESP32 core (version 3.3.5) | LGPL-2.1-or-later | See below. |

**The LGPL.** The LGPL is the "Lesser" GPL. It only covers the library itself,
**not this project's code**. But anyone who distributes a firmware binary
containing it has to, under section 6 of LGPL-2.1:

1. include the LGPL licence text and say that the Arduino-ESP32 core is used;
2. make the core's source available. It's public:
   <https://github.com/espressif/arduino-esp32>, release 3.3.5;
3. let people re-link the firmware with a modified core, which in practice
   means offering this project's source or object files.

If you build the firmware from source, all three are already covered. A
commercial licensee who ships binaries needs to meet them too, for example by
including the notices and offering the object files on request.

Moving the firmware from the Arduino core to plain ESP-IDF, which is Apache-2.0
only, would remove the LGPL conditions altogether. That's worth considering
before any large commercial use.

## Contributions

Contributions are welcome. Because the project is also licensed commercially,
contributors may be asked to sign a short agreement letting their changes go
into commercially licensed versions too. Without one, a contribution can only
be used under CC BY-NC-SA.
