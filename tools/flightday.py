#!/usr/bin/env python3
"""Simulate a flight day: FPVGate changes channel every few minutes, as a race
director would for each pilot, and the C5 and FPVGate are logged throughout.

Usage:
    python tools/flightday.py COM29 http://192.168.0.201
    python tools/flightday.py COM29 http://192.168.0.201 --dwell 60 --out day.csv

Each step tells FPVGate to change channel (POST /config), so the change goes
over SPI exactly as it would on the day. Then, every --every seconds, it logs
what the C5 is tuned to and reads, and FPVGate's live RSSI. At the end it
prints a summary per channel and puts FPVGate back on the channel it started on.

Meant to run with the VTX off: every channel should then sit at the floor.
Flags: the C5 not on FPVGate's channel, the C5 restarting, the sigma-delta
clock being restarted, FPVGate's RSSI above --high, and creep (the last
minutes of a step reading higher than the first).

The C5 console is opened without resetting the board. FPVGate's RSSI comes
from one event-stream connection kept open for the whole run. Needs pyserial
(bundled with PlatformIO's Python).
"""
import argparse
import json
import re
import statistics
import sys
import threading
import time
import urllib.request

import serial

# (label, MHz, FPVGate bandIndex, channelIndex). Bands: A=0 B=1 E=2 F=3 R=4.
PLAN = [
    ("R1", 5658, 4, 0), ("R2", 5695, 4, 1), ("R3", 5732, 4, 2), ("F2", 5760, 3, 1),
    ("R4", 5769, 4, 3), ("R5", 5806, 4, 4), ("F5", 5820, 3, 4), ("R6", 5843, 4, 5),
    ("A1", 5865, 0, 0), ("R7", 5880, 4, 6), ("E6", 5905, 2, 5), ("R8", 5917, 4, 7),
]


class C5:
    def __init__(self, port):
        self.s = serial.Serial()
        self.s.port = port
        self.s.baudrate = 115200
        self.s.timeout = 0.05
        self.s.dtr = False      # don't reset the board when opening
        self.s.rts = False
        self.s.open()

    def cmd(self, c, wait=0.7):
        self.s.reset_input_buffer()
        self.s.write((c + "\n").encode())
        end = time.time() + wait
        buf = b""
        while time.time() < end:
            buf += self.s.read(4096)
        return buf.decode(errors="replace")

    def sample(self):
        d = self.cmd("diag")
        b = self.cmd("bus")
        out = {}
        m = re.search(r"f=(\d+) \S+ st=(\w+) db=(-?[\d.]+) rssi=(\d+)", d)
        if m:
            out.update(c5_mhz=int(m.group(1)), state=m.group(2), db=float(m.group(3)),
                       counts=int(m.group(4)))
        m = re.search(r"tuned by (\w+)", d)
        if m:
            out["by"] = m.group(1)
        m = re.search(r"phy freq=(\d+)", d)
        if m:
            out["phy_mhz"] = int(m.group(1))
        m = re.search(r"chip (-?[\d.]+) C", d)
        if m:
            out["temp"] = float(m.group(1))
        m = re.search(r"frames=(\d+) writes=(\d+)", b)
        if m:
            out["frames"] = int(m.group(1))
            out["writes"] = int(m.group(2))
        m = re.search(r"restarted (\d+) times", b)
        if m:
            out["sdm_fix"] = int(m.group(1))
        return out


class FPVGate:
    def __init__(self, base):
        self.base = base.rstrip("/")
        self.lock = threading.Lock()
        self.samples = []          # (time, rssi) from the event stream
        self.stream_ok = False
        threading.Thread(target=self._stream, daemon=True).start()

    def _post(self, path, body=None):
        data = json.dumps(body).encode() if body is not None else b""
        req = urllib.request.Request(self.base + path, data=data, method="POST")
        if body is not None:
            req.add_header("Content-Type", "application/json")
        with urllib.request.urlopen(req, timeout=5) as r:
            return r.read().decode(errors="replace")

    def config(self):
        with urllib.request.urlopen(self.base + "/config", timeout=5) as r:
            return json.loads(r.read().decode(errors="replace"))

    def set_channel(self, mhz, band, chan):
        self._post("/config", {"freq": mhz, "bandIndex": band, "channelIndex": chan})

    def _stream(self):
        # One connection for the whole run; reconnect if it drops.
        while True:
            try:
                self._post("/timer/rssiStart")
                req = urllib.request.Request(self.base + "/events",
                                             headers={"Accept": "text/event-stream"})
                with urllib.request.urlopen(req, timeout=30) as r:
                    self.stream_ok = True
                    event = None
                    for raw in r:
                        line = raw.decode(errors="replace").strip()
                        if line.startswith("event:"):
                            event = line[6:].strip()
                        elif line.startswith("data:") and event == "rssi":
                            v = line[5:].strip()
                            if v.isdigit():
                                with self.lock:
                                    self.samples.append((time.time(), int(v)))
            except Exception:
                self.stream_ok = False
                time.sleep(3)

    def since(self, t):
        with self.lock:
            return [v for (ts, v) in self.samples if ts >= t]


def summarise(rows, high):
    print("\nch    MHz   C5 on  by    C5 dBm mean/max   FPVGate mean/max  creep  flags")
    for label, mhz, _, _ in PLAN:
        rs = [r for r in rows if r["step"] == label]
        if not rs:
            continue
        dbs = [r["db"] for r in rs if "db" in r]
        fg = [v for r in rs for v in r["fpv"]]
        flags = []
        if any(r.get("c5_mhz") != mhz for r in rs[1:]):
            flags.append("C5-OFF-CHANNEL")
        if any(r.get("by") == "phy" and r.get("phy_mhz") not in (None, r.get("c5_mhz"))
               for r in rs):
            flags.append("PHY-MOVED")
        if fg and max(fg) > high:
            flags.append("FPVGATE>%d" % high)
        n = max(1, len(rs) // 5)
        first = [v for r in rs[:n] for v in r["fpv"]]
        last = [v for r in rs[-n:] for v in r["fpv"]]
        creep = (statistics.mean(last) - statistics.mean(first)) if first and last else 0.0
        if creep > 10:
            flags.append("CREEP")
        by = rs[-1].get("by", "?")
        print("%-5s %4d  %5s  %-4s  %6.1f / %6.1f    %5.1f / %3d      %+5.1f  %s" % (
            label, mhz, rs[-1].get("c5_mhz", "?"), by,
            statistics.mean(dbs) if dbs else 0, max(dbs) if dbs else 0,
            statistics.mean(fg) if fg else 0, max(fg) if fg else 0, creep,
            " ".join(flags) or "ok"))
    fixes = [r["sdm_fix"] for r in rows if "sdm_fix" in r]
    if fixes and fixes[-1] > fixes[0]:
        print("sigma-delta clock restarted %d times during the run" % (fixes[-1] - fixes[0]))
    resets = sum(1 for a, b in zip(rows, rows[1:])
                 if "frames" in a and "frames" in b and b["frames"] < a["frames"])
    if resets:
        print("C5 RESTARTED %d times (bus frame counter went backwards)" % resets)
    temps = [r["temp"] for r in rows if "temp" in r]
    if temps:
        print("chip temperature %.1f to %.1f C" % (min(temps), max(temps)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("fpvgate", help="FPVGate's address, e.g. http://192.168.0.201")
    ap.add_argument("--dwell", type=float, default=600, help="seconds per channel (default 600)")
    ap.add_argument("--every", type=float, default=10, help="seconds between samples (default 10)")
    ap.add_argument("--high", type=int, default=60, help="flag FPVGate RSSI above this (default 60)")
    ap.add_argument("--out", default="flightday.csv")
    ap.add_argument("--only", help="run just these channels, e.g. R4,R8")
    args = ap.parse_args()
    if args.only:
        keep = set(args.only.upper().split(","))
        PLAN[:] = [p for p in PLAN if p[0] in keep]

    c5 = C5(args.port)
    fg = FPVGate(args.fpvgate)
    start_cfg = fg.config()
    print("FPVGate starts on %s MHz; %d steps of %.0f s (%.1f h)" % (
        start_cfg.get("freq"), len(PLAN), args.dwell, len(PLAN) * args.dwell / 3600))

    rows = []
    t_run = time.time()
    with open(args.out, "w") as f:
        f.write("t_s,step,mhz,c5_mhz,by,phy_mhz,db,counts,fpv_mean,fpv_max,sdm_fix,frames,temp\n")
        try:
            for label, mhz, band, chan in PLAN:
                # FPVGate only tunes the receiver when the channel changes, so
                # if it's already on this one, move it away first.
                if fg.config().get("freq") == mhz:
                    other = PLAN[0] if PLAN[0][1] != mhz else PLAN[-1]
                    if other[1] == mhz:
                        other = ("A4", 5805, 0, 3)
                    fg.set_channel(other[1], other[2], other[3])
                    time.sleep(1.5)
                writes = c5.sample().get("writes")
                fg.set_channel(mhz, band, chan)
                t_step = time.time()
                time.sleep(1.5)
                after = c5.sample()
                spi = "SPI tune seen" if (writes is not None and after.get("writes", 0) > writes
                                          and after.get("c5_mhz") == mhz) else "SPI TUNE NOT SEEN"
                print("\n[%5.0f s] %s %d MHz  (%s, tuned by %s)" % (
                    t_step - t_run, label, mhz, spi, after.get("by", "?")), flush=True)
                t_last = t_step
                while time.time() - t_step < args.dwell:
                    time.sleep(max(0, args.every - (time.time() - t_last)))
                    s = c5.sample()
                    vals = fg.since(t_last)
                    t_last = time.time()
                    s.update(step=label, fpv=vals)
                    rows.append(s)
                    line = [
                        "%.0f" % (t_last - t_run), label, str(mhz), str(s.get("c5_mhz", "")),
                        s.get("by", ""), str(s.get("phy_mhz", "")), str(s.get("db", "")),
                        str(s.get("counts", "")),
                        "%.1f" % statistics.mean(vals) if vals else "",
                        str(max(vals)) if vals else "", str(s.get("sdm_fix", "")),
                        str(s.get("frames", "")), str(s.get("temp", "")),
                    ]
                    f.write(",".join(line) + "\n")
                    f.flush()
                    warn = ""
                    if s.get("c5_mhz") != mhz:
                        warn += "  <-- C5 on %s" % s.get("c5_mhz")
                    if vals and max(vals) > args.high:
                        warn += "  <-- FPVGate %d" % max(vals)
                    if not fg.stream_ok:
                        warn += "  (FPVGate stream down)"
                    print("  %5.0f s  C5 %s %s dBm out %s  FPVGate %s%s" % (
                        t_last - t_step, s.get("c5_mhz", "?"), s.get("db", "?"),
                        s.get("counts", "?"), line[8] or "-", warn), flush=True)
        except KeyboardInterrupt:
            print("\nstopped early")
        finally:
            band, chan = start_cfg.get("bandIndex"), start_cfg.get("channelIndex")
            try:
                fg.set_channel(start_cfg.get("freq"), band, chan)
                print("FPVGate put back on %s MHz" % start_cfg.get("freq"))
            except Exception as e:
                print("couldn't put FPVGate back: %s" % e)
    summarise(rows, args.high)


if __name__ == "__main__":
    sys.exit(main())
