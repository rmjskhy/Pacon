"""Bounded read-only OuO timing capture; never prints unrelated serial logs.

Legacy firmware's 'avg render' measures flush only. New firmware reports draw,
flush, work and decode separately. Fast flush alone does NOT prove smoothness.
"""
import argparse
import re
import time
import statistics
import json
from pathlib import Path
import serial

parser = argparse.ArgumentParser()
parser.add_argument('--port', default='COM11')
parser.add_argument('--seconds', type=float, default=20)
parser.add_argument('--budget-us', type=int, default=33000)
parser.add_argument('--active-cadence', action='store_true', help='Only use while actively playing; idle holds legitimately lower the rate.')
parser.add_argument('--output', type=Path)
parser.add_argument('--replay', type=Path, help='Re-evaluate a saved real-device capture without requiring the screen to stay awake.')
args = parser.parse_args()
samples = []
blocks = []
metrics = {name: [] for name in ('draw', 'work', 'decode')}
if args.replay:
    saved = json.loads(args.replay.read_text(encoding='utf8'))
    samples, blocks = saved['flush_us'], saved['blocks_ms_frames']
    metrics = saved.get('metrics_us', metrics)
else:
    connection = serial.Serial(port=None, baudrate=115200, timeout=.3)
    connection.dtr = False
    connection.rts = False
    connection.port = args.port
    connection.open()
    with connection:
        end = time.monotonic() + args.seconds
        while time.monotonic() < end:
            line = connection.readline().decode(errors='replace').strip()
            match = re.search(r'\[0u0-PERF\].*avg render=(\d+)us', line)
            if match:
                fields = dict((name, int(value)) for name, value in
                              re.findall(r'\b(draw|flush|work|decode)=(\d+)us', line))
                samples.append(fields.get('flush', int(match[1])))
                for name in metrics:
                    if name in fields:
                        metrics[name].append(fields[name])
                stamp = re.search(r'I \((\d+)\).*frames=(\d+)', line)
                if stamp:
                    blocks.append((int(stamp[1]), int(stamp[2])))
                print(line, flush=True)
if not samples:
    print('NO_TIMING_SAMPLES: keep the device on the OuO screen during capture.')
    raise SystemExit(2)
worst = max(samples)
print(f'FLUSH_LOWER_BOUND: worst_average={worst}us budget={args.budget_us}us samples={len(samples)}')
if metrics.get('work'):
    for name, values in metrics.items():
        if values:
            print(f'{name.upper()}: median={statistics.median(values):.0f}us worst_window={max(values)}us')
else:
    print('NOTE: legacy firmware timing excludes pixel rendering and decoding.')
cadence = [(b[0]-a[0])/(b[1]-a[1]) for a,b in zip(blocks,blocks[1:]) if b[1]>a[1] and b[0]>a[0]]
result = {'flush_us':samples, 'blocks_ms_frames':blocks, 'ms_per_update':cadence, 'metrics_us':metrics}
if args.output:
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result,indent=2),encoding='utf8')
if args.active_cadence:
    if len(cadence)<3:
        print('INSUFFICIENT_ACTIVE_BLOCKS')
        raise SystemExit(2)
    median = statistics.median(cadence)
    print(f'ACTIVE_CADENCE: median={median:.2f}ms/update, target<=50ms/update, {1000/median:.2f}updates/s')
    raise SystemExit(1 if median>50 else 0)
raise SystemExit(1 if max(metrics.get('work') or samples) > args.budget_us else 0)
