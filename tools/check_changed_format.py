#!/usr/bin/env python3
"""Check added/changed lines without reformatting inherited explanatory comments."""
import pathlib
import subprocess
import sys
base = 'f3a9677'
changes = subprocess.check_output(['git', 'diff', '--no-ext-diff', '--unified=0', base, '--', '*.cpp', '*.hpp', '*.c', '*.h', '*.py', '*.sh', '*.yml', 'CMakeLists.txt'], text=True)
failed = []
path = ''
for line in changes.splitlines():
    if line.startswith('+++ b/'): path = line[6:]
    elif line.startswith('+') and not line.startswith('+++'):
        body = line[1:]
        if body.rstrip() != body or '\t' in body:
            failed.append(path + ': trailing whitespace or tab in added line')
for p in subprocess.check_output(['git', 'ls-files', '--others', '--exclude-standard'], text=True).splitlines():
    f = pathlib.Path(p)
    if f.suffix in {'.cpp', '.hpp', '.c', '.h', '.py', '.sh', '.yml'}:
        if any(line.rstrip() != line or '\t' in line for line in f.read_text().splitlines()):
            failed.append(p + ': trailing whitespace or tab')
print('\n'.join(failed) if failed else 'PASS: changed-line whitespace formatting')
sys.exit(bool(failed))
