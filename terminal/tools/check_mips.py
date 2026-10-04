#!/usr/bin/env python3
"""Build and inspect the actual integration target, including static Rime.

Run inside c1max-apps-builder:bookworm after configuring apps/.build/mips.
"""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[2]
build = root / '.build/mips'
if not (build / 'CMakeCache.txt').is_file():
    raise SystemExit('Configure the MIPS integration build first with tools/build.sh')
subprocess.run(['cmake', '--build', str(build), '--target', 'c1max-terminal', '-j4'], check=True)
binary = build / 'c1max-terminal'
header = subprocess.check_output(['mipsel-linux-gnu-readelf', '-h', str(binary)], text=True)
program = subprocess.check_output(['mipsel-linux-gnu-readelf', '-l', str(binary)], text=True)
dynamic = subprocess.check_output(['mipsel-linux-gnu-readelf', '-d', str(binary)], text=True)
assert 'ELF32' in header and 'little endian' in header and 'mips32r2' in header
assert 'INTERP' not in program and '(NEEDED)' not in dynamic
print('Static MIPS terminal including Rime:', binary.stat().st_size, 'bytes')
