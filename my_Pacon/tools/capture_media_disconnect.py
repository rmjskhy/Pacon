"""Bounded PACON serial capture for the read-only media-refresh reproduction.

Start this before connecting the phone and refreshing its media list. Captures
only relevant firmware diagnostics, not credentials or unrelated app logs.
"""
import argparse
import re
import time
from pathlib import Path
import serial

p = argparse.ArgumentParser()
p.add_argument('--seconds', type=float, default=45)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
lines = []
pattern = re.compile(r'MEDIA|media|disconnect|Disconnect|Guru|panic|Backtrace|Stack|stack|watchdog|rst:|abort|assert|reboot|Reset|BLE.*connect')
port = serial.Serial(port=None, baudrate=115200, timeout=.3)
port.dtr = False
port.rts = False
port.port = 'COM11'
port.open()
with port:
    end = time.monotonic() + a.seconds
    while time.monotonic() < end:
        line = port.readline().decode(errors='replace').strip()
        if pattern.search(line):
            line = re.sub(r'(?i)(SET WIFI\s+).*', r'\1[redacted]', line)
            lines.append(line)
            print(line, flush=True)
a.output.parent.mkdir(parents=True, exist_ok=True)
a.output.write_text('\n'.join(lines), encoding='utf8')
if any(re.search(r'Guru|panic|Backtrace|abort|assert|disconnect', line, re.I) for line in lines):
    raise SystemExit(1)
print('NO_FAILURE_CAPTURED' if lines else 'NO_RELEVANT_SAMPLES')
