#!/usr/bin/env python3
import pathlib,subprocess
root=pathlib.Path(__file__).resolve().parents[2]
out=root/'.build/hidpilot-tests';out.mkdir(exist_ok=True)
subprocess.run(['c++','-std=c++17','-fsanitize=address,undefined','-g','hidpilot/tests/core_test.cpp','-o',str(out/'wire-test')],cwd=root,check=True)
subprocess.run([str(out/'wire-test')],check=True)
