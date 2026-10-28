#!/usr/bin/env python3
"""Compare chapter logs with the frozen 116195b4 C++ parser and Expat GE=0."""
import argparse
from collections import Counter
import gzip
import json
from pathlib import Path
import os
import subprocess
import tempfile
import zipfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def corpus(directory):
    index = 0
    for epub in sorted((ROOT / 'test/epubs').glob('*.epub')):
        with zipfile.ZipFile(epub) as archive:
            for name in sorted(archive.namelist()):
                if name.endswith(('.xhtml', '.html', '.htm')):
                    (directory / f'book-{index:04d}.xhtml').write_bytes(archive.read(name))
                    index += 1
    seed = b'<html>\n<body><p class="x">word &amp; text</p></body></html>'
    for index in range(len(seed) + 1):
        (directory / f'truncate-{index:03d}.xhtml').write_bytes(seed[:index])
    for index in range(len(seed)):
        mutated = bytearray(seed)
        mutated[index] ^= 0x20
        (directory / f'flip-{index:03d}.xhtml').write_bytes(mutated)
    for index, tail in enumerate([b'junk', b'<broken', b'<!--', b'\xff', b'<p/>']):
        (directory / f'trailing-{index}.xhtml').write_bytes(seed + tail)


def records(path):
    result = {}
    for line in path.read_text().splitlines():
        key, value = line.split('\t')
        if key == 'log':
            result[case]['logs'].append(bytes.fromhex(value).decode())
        else:
            case = key
            result[case] = {'ok': value, 'logs': []}
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--binary', type=Path, required=True)
    args = parser.parse_args()
    expected = json.loads(gzip.decompress((HERE / 'reference.json.gz').read_bytes()))
    with tempfile.TemporaryDirectory(prefix='chapter-diagnostics-') as work:
        work = Path(work)
        inputs = work / 'inputs'
        inputs.mkdir()
        corpus(inputs)
        output = work / 'logs.tsv'
        env = dict(os.environ, CHAPTER_DIAGNOSTIC_CORPUS=str(inputs), CHAPTER_DIAGNOSTIC_LOGS=str(output))
        subprocess.run([str(args.binary.resolve()), '--gtest_filter=ChapterDiagnostics.Corpus'], env=env, check=True)
        actual = records(output)
    assert actual.keys() == expected.keys(), 'Case inventory changed'
    differences = []
    for case, old in expected.items():
        new = actual[case]
        if old['ok'] != new['ok'] or Counter(old['logs']) != Counter(new['logs']):
            differences.append(case)
    # Pre-existing XML precedence: both accept trailing malformed markup, but the
    # Rust scanner detects its incomplete token before checking the document end.
    exception = 'trailing-1.xhtml'
    assert differences == [exception], differences
    assert expected[exception] == {
        'ok': '1', 'logs': ['DBG:EHP:Ignoring trailing data after </html>: junk after document element']}
    assert actual[exception] == {
        'ok': '1', 'logs': ['DBG:EHP:Ignoring trailing data after </html>: unclosed token']}
    print(f'{len(actual) - 1}/{len(actual)} exact multisets; one approved pre-existing XML diagnostic difference')


if __name__ == '__main__':
    main()
