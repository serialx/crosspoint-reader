#!/usr/bin/env python3
"""Prove that the drawing oracle independently rejects pixel and advance changes.

Run with a dedicated TextGolden CMake build directory and an evidence directory.
Only the generated renderer copy is mutated; it is restored and rebuilt on exit.
"""
import argparse
import itertools
import pathlib
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=pathlib.Path)
    parser.add_argument('evidence', type=pathlib.Path)
    args = parser.parse_args()
    build, evidence = args.build.resolve(), args.evidence.resolve()
    evidence.mkdir(parents=True, exist_ok=True)

    def rebuild(label):
        with (evidence / (label + '-build.log')).open('w') as log:
            subprocess.run(['cmake', '--build', str(build), '--target', 'TextGolden', '-j6'],
                           stdout=log, stderr=subprocess.STDOUT, check=True)

    def check(label, expected):
        with (evidence / (label + '.log')).open('w') as log:
            result = subprocess.run([sys.executable, str(HERE / 'draw_run.py'), 'check',
                                     '--binary', str(build / 'TextGolden'),
                                     '--work-dir', str(evidence / label)],
                                    stdout=log, stderr=subprocess.STDOUT)
        if result.returncode != expected:
            raise RuntimeError(f'{label}: expected exit {expected}, got {result.returncode}')

    rebuild('baseline')
    check('baseline', 0)
    source = build / 'instrumented/GfxRenderer.cpp'
    original = source.read_text()
    pixel_anchor = '  if (renderer.grayPlanesAreAbsolute()) renderMode = GfxRenderer::BW;'
    if original.count(pixel_anchor) != 2:
        raise RuntimeError('Review the pixel mutation anchors')
    scaled_start = original.index('  return xp_native_draw_scaled(')
    scaled_end = original.index('  });', scaled_start)
    mutations = {
        'pixel': original.replace(pixel_anchor, pixel_anchor + '\n  cursorX += cp == 65 ? 1 : 0;'),
        # U+10FFFF is last in every frozen "missing" input: no following cursor
        # or bitmap changes can mask whether the returned value is observed.
        'advance': original[:scaled_end] + '  }) + (cp == 0x10ffff ? 1 : 0);' + original[scaled_end + 5:],
    }
    try:
        for label, mutated in mutations.items():
            source.write_text(mutated)
            rebuild(label)
            check(label, 1)
            changed = 0
            with (evidence / 'baseline/records.tsv').open() as before, \
                    (evidence / label / 'records.tsv').open() as after:
                for a, b in itertools.zip_longest(before, after):
                    assert a is not None and b is not None, 'Record count changed'
                    old, new = a.rstrip('\n').split('\t'), b.rstrip('\n').split('\t')
                    assert old[0] == new[0], 'Case order changed'
                    if label == 'pixel':
                        assert old[1].split('|')[0] == new[1].split('|')[0], 'Glyph trace changed'
                        assert old[2:] == new[2:], 'Returned advances changed'
                    else:
                        assert old[1] == new[1], 'Glyph trace or pixels changed'
                    changed += a != b
            assert changed > 0, 'Mutation did not affect records'
            (evidence / (label + '-proof.txt')).write_text(
                f'{changed} records changed only in {label} output; unchanged archive rejected them.\n')
    finally:
        source.write_text(original)
        rebuild('restored')
    check('restored', 0)


if __name__ == '__main__':
    main()
