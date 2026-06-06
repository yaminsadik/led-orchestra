#!/usr/bin/env python3
"""Minimal serial console helper for the Stage 0 boards.

Usage:
  serlog.py PORT [--dur SECONDS] [--reset] [--cmd "console command"]

--reset pulses the ESP32-C6 USB-Serial/JTAG reset (RTS=EN) for a fresh boot.
--cmd sends a console command (CR+LF terminated) before reading.
Reads and prints raw serial output for DUR seconds.
"""
import sys, time, serial

port = sys.argv[1]
dur = 10.0
do_reset = "--reset" in sys.argv
cmd = None
if "--dur" in sys.argv:
    dur = float(sys.argv[sys.argv.index("--dur") + 1])
if "--cmd" in sys.argv:
    cmd = sys.argv[sys.argv.index("--cmd") + 1]

ser = serial.Serial(port, 115200, timeout=0.1)

if do_reset:
    # C6 USB-JTAG: DTR low (boot pin high -> normal boot), pulse RTS(EN) low.
    ser.dtr = False
    ser.rts = True
    time.sleep(0.2)
    ser.rts = False
    time.sleep(0.05)
else:
    ser.reset_input_buffer()

if cmd is not None:
    ser.write((cmd + "\r\n").encode())

end = time.time() + dur
while time.time() < end:
    data = ser.read(8192)
    if data:
        sys.stdout.buffer.write(data)
        sys.stdout.flush()
ser.close()
