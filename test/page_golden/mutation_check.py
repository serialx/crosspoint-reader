#!/usr/bin/env python3
"""Mutate only generated build copies, check one frozen public case, then restore."""
import argparse
import json
import pathlib
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import run as oracle


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('build', type=pathlib.Path)
    ap.add_argument('evidence', type=pathlib.Path)
    args = ap.parse_args()
    build, evidence = args.build.resolve(), args.evidence.resolve()
    evidence.mkdir(parents=True, exist_ok=True)
    case = 'synthetic/public/0'

    def rebuild(label):
        # Make may round mtimes to seconds; remove objects without cleaning away generated mutants.
        for obj in (build / 'CMakeFiles/PageGolden.dir/instrumented').glob('*.o'):
            obj.unlink()
        with (evidence / f'{label}-build.log').open('w') as log:
            subprocess.run(['cmake', '--build', str(build), '--target', 'PageGolden', '-j6'],
                           stdout=log, stderr=subprocess.STDOUT, check=True)

    def check(label, expected):
        with (evidence / f'{label}.log').open('w') as log:
            result = subprocess.run([sys.executable, str(HERE / 'run.py'), 'check', '--case', case,
                                     '--binary', str(build / 'PageGolden'), '--work-dir', str(evidence / label)],
                                    stdout=log, stderr=subprocess.STDOUT)
        if result.returncode != expected:
            raise RuntimeError(f'{label}: expected {expected}, got {result.returncode}')
        return oracle.parse_records(evidence / label / 'case-0072/records.tsv')

    rebuild('baseline')
    baseline = check('baseline', 0)
    paths = {name: build / 'instrumented' / name for name in ('GfxRenderer.cpp', 'TextBlock.cpp', 'Page.cpp')}
    originals = {name: path.read_text() for name, path in paths.items()}
    mutations = [
        ('pixel', 'GfxRenderer.cpp',
         '        textGoldenGlyph(*c.font, glyph->codepoint, c.paint->x + glyph->x, c.paint->y + glyph->y, c.style);',
         '        textGoldenGlyph(*c.font, glyph->codepoint, c.paint->x + glyph->x, c.paint->y + glyph->y, c.style);\n'
         '        static bool mutated = false;\n'
         '        if (!mutated) { c.paint->target.buffer[0] ^= 0x80; mutated = true; }'),
        ('word-x', 'TextBlock.cpp', '  return block;',
         '  if (wc) const_cast<int16_t*>(block->xposArr)[0] += 1;\n  return block;'),
        ('footnote', 'Page.cpp', '  return page;',
         '  if (!page->footnotes.empty()) page->footnotes.pop_back();\n  return page;'),
        ('page-byte', 'Page.cpp', '    const auto& fn = footnotes[i];',
         '    auto fn = footnotes[i];\n    fn.number[sizeof(fn.number) - 1] ^= 1;'),
    ]
    proof = {}
    try:
        for label, name, anchor, replacement in mutations:
            if originals[name].count(anchor) != 1:
                raise RuntimeError(f'{label}: mutation anchor changed')
            paths[name].write_text(originals[name].replace(anchor, replacement))
            rebuild(label)
            candidate = check(label, 1)
            changed = {}
            for page, a in baseline['pages'].items():
                b = candidate['pages'][page]
                changes = [field for field in a if a[field] != b[field]]
                if changes:
                    changed[page] = changes
                if label == 'pixel':
                    assert a['records_sha256'] == b['records_sha256']
                    assert a['raw_sha256'] == b['raw_sha256']
                if label == 'page-byte':
                    assert a['records_sha256'] == b['records_sha256']
                    assert a['pixels'] == b['pixels']
            assert changed, label + ' did not affect the observed page'
            if label == 'pixel':
                before = (evidence / 'baseline/case-0072/sample-0.pbm').read_bytes()
                after = (evidence / label / 'case-0072/sample-0.pbm').read_bytes()
                assert len(before) == len(after)
                assert sum(bin(a ^ b).count('1') for a, b in zip(before, after)) == 1
            proof[label] = changed
            paths[name].write_text(originals[name])
    finally:
        for name, path in paths.items():
            path.write_text(originals[name])
        rebuild('restored')
    check('restored', 0)
    (evidence / 'proof.json').write_text(json.dumps(proof, indent=2) + '\n')
    print(json.dumps(proof, indent=2))


if __name__ == '__main__':
    main()
