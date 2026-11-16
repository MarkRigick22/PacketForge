#!/usr/bin/env python3
"""Check exact original comment tokens, in order within each original file."""
import pathlib
import re
import subprocess
import sys

BASE = 'f3a9677'
# Skip string/character literals before recognizing C/C++ comments.
LEX = re.compile(r'R"([^ ()\\\t\r\n]{0,16})\(.*?\)\1"|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*.*?\*/', re.S)
def comments(path, text):
    if pathlib.Path(path).suffix in {'.cpp', '.hpp', '.c', '.h'}:
        return [m.group() for m in LEX.finditer(text) if m.group().startswith(('//', '/*'))]
    if pathlib.Path(path).suffix in {'.py', '.sh', '.cmake'} or path.endswith('CMakeLists.txt'):
        return [line for line in text.splitlines() if line.lstrip().startswith('#')]
    return []

failures = []
count = 0
for path in subprocess.check_output(['git', 'ls-tree', '-r', '--name-only', BASE], text=True).splitlines():
    old = subprocess.check_output(['git', 'show', f'{BASE}:{path}']).decode('utf-8')
    original = comments(path, old)
    if not original:
        continue
    current = comments(path, pathlib.Path(path).read_text()) if pathlib.Path(path).exists() else []
    it = iter(current)
    for comment in original:
        count += 1
        if not any(token == comment for token in it):
            failures.append((path, comment))
            break
for path, comment in failures:
    print(f'FAIL {path}: {comment}')
print(f'{"FAIL" if failures else "PASS"}: {count} original comments against {BASE}; exact wording and order')
sys.exit(bool(failures))
