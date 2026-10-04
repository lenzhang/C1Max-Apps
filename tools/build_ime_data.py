#!/usr/bin/env python3
"""Compile Rime data with the target deployer on the build host, never on device."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('deployer', type=Path)
args = parser.parse_args()
deployer = args.deployer.resolve()
output = root / '.build/rime-data'
required = ('luna_pinyin_simp.prism.bin', 'luna_pinyin.table.bin', 'luna_pinyin_simp.schema.yaml')
# Linux temporary storage avoids a shared-folder penalty while compiling.
with tempfile.TemporaryDirectory(prefix='c1max-rime-data-') as tmp:
    directory = Path(tmp)
    shared, user, staging = (directory / p for p in ('shared', 'user', 'build'))
    shutil.copytree(root / 'terminal/assets/rime-data', shared, ignore=shutil.ignore_patterns('build'))
    user.mkdir()
    staging.mkdir()
    subprocess.run(['qemu-mipsel', str(deployer), '--build', str(user), str(shared), str(staging)], check=True, timeout=600)
    for name in required:
        if not (staging / name).is_file(): raise SystemExit('Rime did not produce ' + name)
    # Replace only generated build output after successful compilation.
    output.mkdir(parents=True, exist_ok=True)
    if (output / 'build').exists(): shutil.rmtree(output / 'build')
    shutil.copytree(staging, output / 'build')
print('Built target-compatible IME dictionaries:', output / 'build')
