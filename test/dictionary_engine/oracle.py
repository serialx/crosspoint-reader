#!/usr/bin/env python3
"""Frozen original-engine outputs; source-root permits an out-of-tree differential run."""
import argparse
import difflib
import os
from pathlib import Path
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / 'test'))
from rust_host import rust_link_flags


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build', type=Path, default=ROOT / 'build/dictionary-engine')
    parser.add_argument('--source-root', type=Path, default=ROOT)
    parser.add_argument('--write-golden', action='store_true')
    parser.add_argument('--check-mutation', action='store_true')
    parser.add_argument('--compressed', action='store_true', help='check compressed lookup failure and retry')
    args = parser.parse_args()
    args.build.mkdir(parents=True, exist_ok=True)
    source = args.source_root
    html = (source / 'src/util/DictHtmlPages.cpp').read_text()
    # Expose the production normalizer, excluding parser/layout dependencies.
    html = html[html.index('namespace {'):html.index('bool buildDictionaryHtmlPages(')]
    unit = '#include <HalStorage.h>\n#include <algorithm>\n#include <cctype>\n#include <cstring>\n#include <xp_ffi.h>\n' + html
    if (source / 'src/util/DictionaryIo.h').exists():
        unit = '#include \"DictionaryIo.h\"\n' + unit
    unit += '\nbool normalize(const std::string& html, HalFile& file) { return writeNormalizedXhtml(html, file); }\n'
    normalizer = args.build / 'Normalize.cpp'
    normalizer.write_text(unit)
    binary = args.build / 'probe'
    command = [os.environ.get('CXX', 'c++'), '-std=c++20', '-O2', '-I', str(HERE / 'stubs'),
               '-I', str(source / 'src/util'), '-I', str(source / 'rust/crates/xp-ffi/include'), '-I', str(ROOT / 'lib/Utf8'), '-I', str(ROOT / 'lib/InflateReader'),
               str(HERE / 'Probe.cpp'), str(normalizer),
               *[str(source / f'src/util/{name}.cpp') for name in ('Dictionary', 'DictionaryRegistry', 'DictZip')],
               str(ROOT / 'lib/Epub/Epub/CjkBreaks.cpp'), str(ROOT / 'lib/Utf8/Utf8.cpp'),
               str(ROOT / 'lib/InflateReader/InflateReader.cpp'), *rust_link_flags(), '-o', str(binary)]
    subprocess.run(command, check=True)
    run = [binary] + (['--compressed'] if args.compressed else [])
    result = subprocess.check_output(run)
    (args.build / 'actual.txt').write_bytes(result)
    golden = HERE / ('golden-compressed.txt' if args.compressed else 'golden.txt')
    if args.write_golden:
        golden.write_bytes(result)
    else:
        expected = golden.read_bytes()
        if result != expected:
            sys.stdout.writelines(difflib.unified_diff(expected.decode().splitlines(True), result.decode().splitlines(True), fromfile='golden', tofile='actual'))
            return 1
    if args.check_mutation and not args.compressed:
        original = normalizer.read_text()
        changed = original.replace('<html><body>', '<html><div>', 1)
        if changed == original:
            changed = original.replace('html.data()), html.size()', 'html.data()), 0', 1)
        assert changed != original
        normalizer.write_text(changed)
        subprocess.run(command, check=True)
        assert subprocess.check_output([binary]) != result, 'oracle accepted the deliberate normalization mutation'
        normalizer.write_text(original)
        subprocess.run(command, check=True)
        print('Deliberate one-line normalization mutation rejected')
    print(f'Dictionary engine oracle: {len(result.splitlines())} lines match')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
