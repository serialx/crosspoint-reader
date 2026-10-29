#!/usr/bin/env python3
"""Compile the dictionary numeric contract in both languages and test owner capacities."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / 'test'))
from rust_host import rust_link_flags


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build', type=Path, default=ROOT / 'build/dictionary-abi')
    parser.add_argument('--check-mutation', action='store_true')
    args = parser.parse_args()
    args.build.mkdir(parents=True, exist_ok=True)
    header = (ROOT / 'rust/crates/xp-ffi/include/xp_ffi.h').read_text()
    abi = ROOT / 'rust/crates/xp-ffi/src/dictionary/abi.rs'
    c_values = dict(re.findall(r'\b(XP_DICTIONARY_\w+) = (\d+),', header))
    rust_values = dict(re.findall(r'pub const (XP_DICTIONARY_\w+): \w+ = (\d+);', abi.read_text()))
    assert c_values.keys() == rust_values.keys(), 'C/Rust dictionary constant sets differ'
    cpp = args.build / 'values.cpp'
    cpp.write_text('#include <xp_ffi.h>\n' + ''.join(
        f'static_assert({name} == {value}, "{name}");\n' for name, value in rust_values.items()))
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-fsyntax-only',
                    '-I', str(ROOT / 'rust/crates/xp-ffi/include'), str(cpp)], check=True)
    rust = args.build / 'values.rs'
    def check_rust(values, succeeds):
        rust.write_text(f'#[path = "{abi.as_posix()}"] mod abi;\n' + ''.join(
            f'#[test] fn {name.lower()}() {{ assert_eq!(abi::{name}, {value}); }}\n'
            for name, value in values.items()))
        binary = args.build / 'values'
        subprocess.run(['rustc', '--test', '--edition=2021', str(rust), '-o', str(binary)], check=True)
        result = subprocess.run([str(binary)], capture_output=True, text=True)
        if (result.returncode == 0) != succeeds:
            raise AssertionError(result.stdout + result.stderr)
    check_rust(c_values, True)
    if args.check_mutation:
        for name in ('XP_DICTIONARY_WORD_BYTES', 'XP_DICTIONARY_SAMPLE_INTERVAL', 'XP_DICTIONARY_PREPARE_OPEN'):
            changed = dict(c_values)
            changed[name] = str(int(changed[name]) + 1)
            check_rust(changed, False)
        check_rust(c_values, True)
        print('Rejected deliberate buffer, sidecar interval and operation drift')
    binary = args.build / 'capacities'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-O2',
                    '-I', str(HERE / 'stubs'), '-I', str(ROOT / 'src/util'),
                    str(HERE / 'Capacities.cpp'), *rust_link_flags(), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
    print(f'Dictionary ABI: {len(c_values)} values agree; owner-capacity checks passed')


if __name__ == '__main__':
    main()
