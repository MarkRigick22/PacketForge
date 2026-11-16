#!/usr/bin/env python3
"""Repeatable factor-controlled benchmarks; raw CSV and host/build metadata."""
import argparse
import csv
import itertools
import json
import os
import pathlib
import platform
import statistics
import subprocess
import sys

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--binary', default='build-release/packetforge')
p.add_argument('--output', default='measurements.csv')
p.add_argument('--packets', type=int, default=1000000)
p.add_argument('--repeats', type=int, default=5)
p.add_argument('--workers', default='1,2,4,8')
p.add_argument('--sizes', default='64,128,512,1500')
p.add_argument('--flows', default='1,1000,10000')
p.add_argument('--rings', default='64,256')
p.add_argument('--batches', default='8,32,128')
p.add_argument('--skews', default='0,90')
p.add_argument('--policies', default='off,on')
p.add_argument('--prebuilt', default='0,10000')
a = p.parse_args()
if a.repeats < 1 or a.packets < 1: p.error('positive repeats/packets required')

def capture(command):
    try:
        r = subprocess.run(command, text=True, capture_output=True, check=True)
        return r.stdout.strip()
    except (OSError, subprocess.CalledProcessError) as e:
        return 'UNAVAILABLE: ' + str(e)

out = pathlib.Path(a.output); out.parent.mkdir(parents=True, exist_ok=True)
cache = pathlib.Path(a.binary).parent / 'CMakeCache.txt'
metadata = dict(platform=platform.platform(), architecture=platform.machine(), logical_cpus=os.cpu_count(),
                cpu=capture(['sysctl', '-n', 'machdep.cpu.brand_string']) if sys.platform == 'darwin' else capture(['lscpu']),
                physical_cores=capture(['sysctl', '-n', 'hw.physicalcpu']) if sys.platform == 'darwin' else capture(['lscpu', '-p=CORE,SOCKET']),
                kernel=platform.release(), compiler=capture(['c++', '--version']),
                git=capture(['git', 'rev-parse', 'HEAD']), diff=capture(['git', 'diff', '--stat']),
                parameters=vars(a), cmake_cache=cache.read_text() if cache.exists() else 'UNAVAILABLE')
out.with_suffix('.metadata.json').write_text(json.dumps(metadata, indent=2))
fields = ['size', 'flows', 'ring', 'batch', 'skew', 'policy', 'prebuilt']
factors = [a.sizes, a.flows, a.rings, a.batches, a.skews, a.policies, a.prebuilt]
writer = None
with out.open('w', newline='') as f:
    for values in itertools.product(*(x.split(',') for x in factors)):
        case = dict(zip(fields, values))
        for workers in map(int, a.workers.split(',')):
            common = [a.binary, '--packets', str(a.packets), '--workers', str(workers), '--size', case['size'],
                      '--buffer-size', str(max(2048, int(case['size']))), '--flows', case['flows'],
                      '--ring-size', case['ring'], '--batch-size', case['batch'], '--skew-percent', case['skew'],
                      '--prebuilt', case['prebuilt'], '--csv']
            if case['policy'] == 'on': common += ['--rules', 'examples/drop-udp.rules', '--flow-capacity', '1024']
            elif case['policy'] != 'off': p.error('policy must be off,on')
            for mode in ('baseline', 'optimized'):
                subprocess.run(common + ['--' + mode, '--packets', str(min(a.packets, 100000))], check=True, stdout=subprocess.DEVNULL)
            for trial in range(a.repeats):
                for mode in (('baseline', 'optimized') if trial % 2 == 0 else ('optimized', 'baseline')):
                    result = subprocess.run(common + ['--' + mode], text=True, capture_output=True, check=True)
                    row = next(csv.DictReader(result.stdout.splitlines()))
                    if int(row['received']) != int(row['transmitted']) + int(row['drops']) or int(row['buffers_in_use']):
                        raise RuntimeError('accounting failure')
                    row.update(case, trial=trial, offered_pps=float(row['received']) / float(row['seconds']))
                    if writer is None: writer = csv.DictWriter(f, fieldnames=list(row)); writer.writeheader()
                    writer.writerow(row); f.flush()
            print(f'Completed {case}, workers={workers}', flush=True)
subprocess.run([sys.executable, 'tools/analyze_results.py', str(out)], check=True)
