#!/usr/bin/env python3
"""Send console commands to an FPVGateC5RX board and print what it replies.

Usage:
    python tools/c5cmd.py COM29 "s" "scan 40"          # run commands
    python tools/c5cmd.py COM29 --reset                 # reset, show boot log
    python tools/c5cmd.py COM29 --listen 10             # just print for 10 s
    python tools/c5cmd.py COM29 "stream on" --listen 5 "stream off"

Each command waits --wait seconds (default 1.5) for output; `scan` waits until
its "scan done" line. Needs pyserial (bundled with PlatformIO's Python).
"""
import argparse
import sys
import time

import serial


def read_for(port, seconds, until=None):
    end = time.time() + seconds
    buf = ""
    while time.time() < end:
        data = port.read(port.in_waiting or 1)
        if data:
            text = data.decode("utf-8", errors="replace")
            sys.stdout.write(text)
            sys.stdout.flush()
            buf += text
            if until and until in buf:
                return buf
    return buf


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("items", nargs="*", help="commands; '--listen N' may appear between them")
    ap.add_argument("--reset", action="store_true", help="reset the board first and show the boot log")
    ap.add_argument("--wait", type=float, default=1.5)
    args, extra = ap.parse_known_args()

    # Open without raising DTR or RTS. On the C5's built-in USB they work as
    # reset and boot lines, so a normal open would restart the board.
    port = serial.Serial()
    port.port = args.port
    port.baudrate = 115200
    port.timeout = 0.05
    port.dtr = False
    port.rts = False
    port.open()
    if args.reset:
        # Pulse RTS (reset) with DTR (boot) released: a normal restart.
        port.dtr = False
        port.rts = True
        time.sleep(0.1)
        port.rts = False
        time.sleep(0.1)
        port.close()
        time.sleep(1.5)                      # USB re-enumerates after reset
        for _ in range(20):
            try:
                port = serial.Serial()
                port.port, port.baudrate, port.timeout = args.port, 115200, 0.05
                port.dtr = False
                port.rts = False
                port.open()
                break
            except serial.SerialException:
                time.sleep(0.25)
        read_for(port, 3.0)
    else:
        port.reset_input_buffer()

    # Put the commands and any "--listen N" pauses back in order.
    seq = list(args.items)
    i = 0
    while i < len(extra):
        if extra[i] == "--listen" and i + 1 < len(extra):
            seq.append(("listen", float(extra[i + 1])))
            i += 2
        else:
            seq.append(extra[i])
            i += 1

    for item in seq:
        if isinstance(item, tuple):
            read_for(port, item[1])
            continue
        sys.stdout.write(f"\n>>> {item}\n")
        port.write((item + "\n").encode())
        if item.startswith("scan"):
            read_for(port, 30.0, until="scan done")
            read_for(port, 0.3)
        else:
            read_for(port, args.wait)
    port.close()


if __name__ == "__main__":
    main()
