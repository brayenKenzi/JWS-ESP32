"""Compile and RUN the same C++ core as firmware. No Python reimplementation."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--zig', action='store_true', help='Use ziglang installed in this Python environment')
args = parser.parse_args()
compiler = shutil.which('g++') or shutil.which('clang++')
if args.zig or (not compiler and importlib.util.find_spec('ziglang')):
    command = [sys.executable, '-m', 'ziglang', 'c++']
elif compiler:
    command = [compiler]
else:
    sys.exit('Need g++/clang++, or install ziglang==0.13.0 and pass --zig')
build = root / 'test' / '.build'
build.mkdir(exist_ok=True)
data = json.loads((root / 'test' / 'fixtures.json').read_text())
rows = []
for f in data['fixtures']:
    rows.append('{"%s", {%s}, %.8f, %.8f, %d, {%s}}' % (
        f['name'], ','.join(map(str, f['date'])), f['latitude'], f['longitude'],
        f['utcOffsetMinutes'], ','.join(map(str, f['minutes']))))
(build / 'fixtures.h').write_text(
    'struct Fixture { const char* name; jws::Date date; double latitude; double longitude; '
    'int offset; int minutes[jws::PrayerCount]; };\n'
    'static const Fixture fixtures[] = {\n' + ',\n'.join(rows) + '\n};\n')
core = root / 'lib' / 'JwsCore' / 'src'
binary = build / ('core_tests.exe' if os.name == 'nt' else 'core_tests')
command += ['-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-I', str(core), '-I', str(root / 'include'), '-I', str(build),
            str(root / 'test' / 'test_core.cpp'), str(core / 'PrayerCalculator.cpp'),
            str(core / 'Simulation.cpp'), '-o', str(binary)]
print('Compiling native C++ core tests...', flush=True)
subprocess.run(command, check=True, cwd=root)
subprocess.run([str(binary)], check=True, cwd=root)
