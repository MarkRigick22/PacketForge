#!/usr/bin/env python3
"""Summarize real benchmark CSV rows; never assume optimized is faster."""
import argparse
import csv
import statistics
from collections import defaultdict

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('csv_file')
args = parser.parse_args()
groups = defaultdict(list)
with open(args.csv_file, newline='') as stream:
    for row in csv.DictReader(stream):
        groups[(row['mode'], int(row['workers']))].append(row)
print('mode       workers trials median_Mpps median_p99_us total_drops peak_RSS_MiB')
for (mode, workers), rows in sorted(groups.items(), key=lambda item: (item[0][1], item[0][0])):
    median = lambda field: statistics.median(float(row[field]) for row in rows)
    peak = max(int(row['peak_rss_bytes']) for row in rows) / 1024**2
    drops = sum(int(row['drops']) for row in rows)
    print(f'{mode:10} {workers:7} {len(rows):6} {median("pps")/1e6:11.3f} {median("p99_us"):13.3f} {drops:11} {peak:12.2f}')
for workers in sorted({key[1] for key in groups}):
    base = groups.get(('baseline', workers))
    optimized = groups.get(('optimized', workers))
    if base and optimized:
        a = statistics.median(float(r['pps']) for r in optimized)
        b = statistics.median(float(r['pps']) for r in base)
        print(f'{workers} workers: optimized/baseline median throughput = {a/b:.3f}x' if b else f'{workers} workers: zero baseline throughput')
