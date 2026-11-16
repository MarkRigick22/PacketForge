#!/usr/bin/env python3
"""Privileged isolated namespace test; called only by test_tap_namespace.sh."""
import csv
import pathlib
import socket
import subprocess
import sys
import time
from generate_packets import frame

binary = sys.argv[1]
receiver = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(0x0800))
receiver.bind(('pf-out', 0)); receiver.settimeout(0.2)
sender = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(0x0800))
sender.bind(('pf-in', 0))
command = [binary, '--tap', 'pf-in', '--tap-out', 'pf-out', '--workers', '2', '--packets', '100',
           '--rules', str(pathlib.Path('examples/drop-udp.rules').resolve()), '--flow-capacity', '64', '--csv']
process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
try:
    # Wait for the userspace TAP endpoint to attach before transmitting.
    deadline = time.monotonic() + 5
    while pathlib.Path('/sys/class/net/pf-in/carrier').read_text().strip() != '1':
        if process.poll() is not None: raise RuntimeError(process.communicate())
        if time.monotonic() > deadline: raise RuntimeError('TAP attachment timeout')
        time.sleep(0.01)
    expected = {frame(i, 128, 100, 1, 50) for i in range(50)}
    for i in range(100): sender.send(frame(i, 128, 100, 1, 50))
    received = []
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        try: received.append(receiver.recv(65550))
        except socket.timeout:
            if process.poll() is not None: break
    stdout, stderr = process.communicate(timeout=5)
    if process.returncode: raise RuntimeError(stderr)
    rows = list(csv.DictReader(stdout.splitlines())); result = rows[0]
    assert len(received) == 50 and set(received) == expected, (len(received), 'unexpected frame bytes')
    assert int(result['policy_drops']) == 50 and int(result['transmitted']) == 50, result
    assert int(result['buffers_in_use']) == 0 and int(result['egress_drops']) == 0, result
    print('PASS: 100 real frames in, 50 TCP frames out byte-for-byte, 50 UDP policy drops; no leaked buffers')
finally:
    if process.poll() is None:
        process.terminate()
        try: process.wait(timeout=5)
        except subprocess.TimeoutExpired: process.kill(); process.wait()
    sender.close(); receiver.close()
