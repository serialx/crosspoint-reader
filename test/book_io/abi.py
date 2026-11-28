#!/usr/bin/env python3
"""Compile both sides of the residual book contract and reject numeric drift."""
import argparse
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=ROOT / 'build/book-residual-abi')
    parser.add_argument('--check-mutation', action='store_true')
    args = parser.parse_args()
    args.build.mkdir(parents=True, exist_ok=True)
    header = ROOT / 'rust/crates/xp-ffi/include/xp_book_residual.h'
    abi = ROOT / 'rust/crates/xp-core/src/book/residual_abi.rs'
    c_values = dict(re.findall(r'\b(XP_BOOK_\w+)\s*=\s*(\d+),', header.read_text()))
    rust_values = dict(re.findall(r'pub const (XP_BOOK_\w+): u32 = (\d+);', abi.read_text()))
    assert c_values and c_values.keys() == rust_values.keys(), 'C/Rust constant sets differ'
    cpp = args.build / 'values.cpp'
    cpp.write_text('#include <xp_ffi.h>\n#include <xp_book.h>\n' + ''.join(
        f'static_assert({name} == {value}, "{name}");\n' for name, value in rust_values.items()))
    with cpp.open('a') as output:
        for name, package in [('CONTAINER', 'CONTAINER'), ('NCX', 'NCX_PASS'),
                              ('NAV', 'NAV_PASS'), ('OPF', 'OPF_PASS')]:
            output.write(f'static_assert(unsigned(XP_BOOK_DOCUMENT_{name}) == '
                         f'unsigned(XP_PKG_{package}));\n')
    with cpp.open('a') as output:
        output.write('static_assert(unsigned(XpBookPath::HtmlDirectory) == XP_BOOK_PATH_HTML_DIRECTORY);\n')
        output.write('static_assert(unsigned(XpBookPath::SectionsDirectory) == XP_BOOK_PATH_SECTIONS_DIRECTORY);\n')
        output.write('static_assert(unsigned(XpBookPath::HtmlDocument) == XP_BOOK_PATH_HTML_DOCUMENT);\n')
        output.write('static_assert(unsigned(XpBookPath::TextHref) == XP_BOOK_PATH_TEXT_HREF);\n')
        output.write('static_assert(unsigned(XpBookPath::Cover) == XP_BOOK_PATH_COVER);\n')
        output.write('static_assert(unsigned(XpBookPath::ThumbPattern) == XP_BOOK_PATH_THUMB_PATTERN);\n')
        output.write('static_assert(unsigned(XpBookPath::ThumbPrefix) == XP_BOOK_PATH_THUMB_PREFIX);\n')
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-fsyntax-only',
                    '-I', str(header.parent), str(cpp)], check=True)
    c_source = args.build / 'values.c'
    c_source.write_text('#include <xp_ffi.h>\n#include <xp_book.h>\n' + ''.join(
        f'_Static_assert({name} == {value}, \"{name}\");\n' for name, value in rust_values.items()))
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-fsyntax-only',
                    '-I', str(header.parent), str(c_source)], check=True)
    source = args.build / 'values.rs'
    binary = args.build / 'values'

    def check_rust(values, succeeds):
        source.write_text(f'#[path = "{abi.as_posix()}"] mod abi;\n' + ''.join(
            f'#[test] fn {name.lower()}() {{ assert_eq!(abi::{name}, {value}); }}\n'
            for name, value in values.items()))
        subprocess.run(['rustc', '--test', '--edition=2021', str(source), '-o', str(binary)], check=True)
        result = subprocess.run([str(binary)], capture_output=True, text=True)
        assert (result.returncode == 0) == succeeds, result.stdout + result.stderr

    check_rust(c_values, True)
    if args.check_mutation:
        # Exercise every dispatcher/event family, including overloaded field 100.
        for enum in re.findall(r'typedef enum \w+ \{(.*?)\}', header.read_text(), re.S):
            name = re.search(r'\b(XP_BOOK_\w+)\s*=', enum)[1]
            changed = dict(c_values)
            changed[name] = str(int(changed[name]) + 1)
            check_rust(changed, False)
        check_rust(c_values, True)
        print('Rejected deliberate drift in every operation/event family')
    print(f'Book residual ABI: {len(c_values)} values agree')


if __name__ == '__main__':
    main()
