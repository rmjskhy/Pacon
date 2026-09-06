#!/usr/bin/env python3
"""Reset PACON and verify Wi-Fi startup for a selected Tickless policy."""

import argparse
import sys
import time

import serial


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="COM11")
    parser.add_argument("--seconds", type=float, default=12.0)
    parser.add_argument(
        "--expect-tickless",
        choices=("enabled", "disabled"),
        default="enabled",
    )
    args = parser.parse_args()

    with serial.Serial(args.port, 115200, timeout=0.2) as device:
        device.dtr = False
        device.rts = True
        time.sleep(0.1)
        device.rts = False
        deadline = time.monotonic() + args.seconds
        chunks = []
        while time.monotonic() < deadline:
            data = device.read(device.in_waiting or 1)
            if data:
                chunks.append(data)

    output = b"".join(chunks).decode("utf-8", errors="replace")
    print(output, end="")
    expected_marker = f"Tickless Idle {args.expect_tickless}"
    if expected_marker not in output:
        print(f"FAIL: expected boot marker not observed: {expected_marker}", file=sys.stderr)
        return 2
    if "Wi-Fi driver init failed" in output or "ESP_ERR_NO_MEM" in output:
        print("FAIL: Wi-Fi startup exhausted internal RAM", file=sys.stderr)
        return 1
    if "Wi-Fi STA: start=requested" not in output:
        print("FAIL: Wi-Fi STA startup success marker not observed", file=sys.stderr)
        return 3
    print(f"TICKLESS WIFI BOOT CHECK ({args.expect_tickless}): PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
