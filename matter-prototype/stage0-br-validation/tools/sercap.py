#!/usr/bin/env python3
"""Continuous serial capture + command injector.

Usage: sercap.py PORT LOGFILE FIFO
  - Reads serial continuously -> appends raw bytes to LOGFILE (unbuffered).
  - Reads lines from FIFO and writes them to the port as console commands
    (CR+LF terminated). Send "__EXIT__" to stop and release the port.
"""
import sys, time, threading, serial

port, logpath, fifopath = sys.argv[1], sys.argv[2], sys.argv[3]
ser = serial.Serial(port, 115200, timeout=0.05)
stop = threading.Event()

def reader():
    f = open(logpath, "ab", buffering=0)
    while not stop.is_set():
        d = ser.read(4096)
        if d:
            f.write(d)
    f.close()

threading.Thread(target=reader, daemon=True).start()

while not stop.is_set():
    try:
        with open(fifopath, "r") as fi:      # blocks until a writer opens
            for line in fi:
                line = line.rstrip("\n")
                if line == "__EXIT__":
                    stop.set()
                    break
                # Send in small paced chunks so the console RX buffer does not
                # overflow on long lines (e.g. a 198-char Thread dataset arg).
                data = (line + "\r\n").encode()
                for i in range(0, len(data), 16):
                    ser.write(data[i:i + 16])
                    ser.flush()
                    time.sleep(0.03)
    except Exception:
        time.sleep(0.1)

time.sleep(0.2)
ser.close()
