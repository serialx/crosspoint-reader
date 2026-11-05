#!/usr/bin/env python3
"""Freeze additional production drawing paths without changing the T0 archive."""
import argparse
import gzip
import hashlib
import itertools
import json
import pathlib
import subprocess
import tempfile
import time

import run as baseline

HERE, ROOT = baseline.HERE, baseline.ROOT
PATHS = {'text': 0, 'rotated': 1, 'scaled': 2, 'words': 3, 'centered': 4}
LABELS = ('kerning', 'ligatures', 'harakat', 'hebrew', 'mixed', 'combining',
          'orphan', 'solid', 'smallcaps', 'korean', 'chinese', 'missing', 'empty')


def word_lines(documents):
    # Keep an ordered six-word excerpt from each public EPUB, with its source entry.
    lines = {}
    for source, tokens in documents.items():
        book = source.split('/', 1)[0]
        if len(tokens) >= 6 and book not in lines:
            lines[book] = {'source': source, 'words': tokens[:6]}
    for label in ('harakat', 'hebrew', 'mixed', 'combining', 'korean', 'chinese'):
        lines['synthetic-' + label] = {'source': label, 'words': baseline.SYNTHETIC[label].split()}
    return lines


def cases(work, fonts, documents):
    groups, counts = {}, {name: 0 for name in PATHS}
    excerpts = word_lines(documents)
    with (work / 'cases.tsv').open('w') as output:
        def emit(name, font, path, orientation, style=0, black=1, absolute=0, label='kerning',
                 geometry='inside', scale=256, tracking=0, direction=-1, fallback=0, words=()):
            width, height = (480, 800) if orientation in (0, 2) else (800, 480)
            x, y = (41, 201) if path == 'rotated' else (41, 61)
            clip = (-32768, -32768, 65535, 65535)  # Physical screen edges, independent of the UI clip.
            strip_y = strip_rows = 0
            if geometry == 'left':
                x = -8
            elif geometry == 'right':
                x = width - 8
            elif geometry == 'top':
                y = 8 if path in ('scaled', 'rotated') else -8
            elif geometry == 'bottom':
                y = height + 8 if path in ('scaled', 'rotated') else height - 8
            elif geometry == 'clip':
                clip = (x + 5, y - 8 if path in ('scaled', 'rotated') else y + 5, 19, 17)
            elif geometry == 'empty-clip':
                clip = (x, y, 0, 0)
            elif geometry == 'strip':
                row = (479 - x, 479 - y, x, y)[orientation]
                strip_y, strip_rows = max(0, min(463, row - 8)), 17
            group = f'draw/{name}/{path}/{orientation}'
            index = groups.get(group, 0)
            groups[group] = index + 1
            counts[path] += 1
            text = baseline.SYNTHETIC[label] if not words else ' '.join(words)
            key = f'{group}:{index}-{style}-{black}-{absolute}-{geometry}-{scale}-{tracking}-{direction}-{label}'
            fields = [key, font, style, tracking, direction, 1, 1, fallback, 65, 86,
                      baseline.packed(text).hex() or '-', PATHS[path], orientation, black, absolute,
                      x, y, *clip, strip_y, strip_rows, scale, len(words)]
            # Explicit cached offsets: no measurement determines placement during drawing.
            # Descending RTL offsets, mixed styles, SUP/SUB shifts and a ruby-like raised token.
            for i, word in enumerate(words):
                dx = (len(words) - 1 - i if direction == 1 else i) * 57 + (i % 3)
                dy = (0, -9, 6, 0, -24, 0)[i % 6]
                word_style = (style, style | 16, style | 32, style | 1, 16, style | 128)[i % 6]
                fields.extend((dx, dy, word_style, direction, word.encode().hex()))
            output.write(' '.join(map(str, fields)) + '\n')

        for name, font in fonts.items():
            # Main matrix: every style, orientation, color and plane encoding for each script.
            for path in ('text', 'rotated', 'scaled'):
                colors = (1,) if path == 'scaled' else (0, 1)  # Scaled API is always black.
                for orientation, style, black, absolute, label in itertools.product(
                        range(4), baseline.STYLES, colors, (0, 1), LABELS):
                    emit(name, font, path, orientation, style, black, absolute, label,
                         scale=384 if path == 'scaled' else 256)
                # Each clipping edge and strip target is crossed with every orientation and encoding.
                for orientation, style, black, absolute, geometry in itertools.product(
                        range(4), (0, 3, 16, 128), colors, (0, 1),
                        ('left', 'right', 'top', 'bottom', 'clip', 'empty-clip', 'strip')):
                    emit(name, font, path, orientation, style, black, absolute, geometry=geometry,
                         scale=384 if path == 'scaled' else 256)
            for orientation, scale, label in itertools.product(range(4), (-1, 0, 128, 255, 256, 257, 512, 768, 4096),
                                                               ('one', 'combining', 'solid', 'missing')):
                emit(name, font, 'scaled', orientation, scale=scale, label=label)
            for orientation, direction, tracking, label in itertools.product(
                    range(4), (-1, 0, 1), (-2, 0, 4), ('harakat', 'hebrew', 'mixed', 'combining')):
                emit(name, font, 'text', orientation, label=label, tracking=tracking, direction=direction)
            for orientation, black, label in itertools.product(range(4), (0, 1), ('kerning', 'mixed')):
                emit(name, font, 'centered', orientation, black=black, label=label)
            for orientation, absolute, geometry, direction, (label, excerpt) in itertools.product(
                    range(4), (0, 1), ('inside', 'clip', 'strip'), (0, 1), excerpts.items()):
                emit(name, font, 'words', orientation, absolute=absolute, geometry=geometry,
                     direction=direction, label=label, words=excerpt['words'], tracking=2)
            if name.startswith(('sd-', 'private-')):
                for path, orientation, geometry, label in itertools.product(
                        ('text', 'rotated'), range(4), ('inside', 'clip', 'strip'),
                        ('harakat', 'hebrew', 'korean', 'chinese')):
                    emit('fallback-' + name, baseline.BUILTINS['ubuntu10'], path, orientation,
                         fallback=font, geometry=geometry, label=label)
    return groups, counts, excerpts


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('action', choices=('capture', 'check'))
    ap.add_argument('--binary', type=pathlib.Path, default=ROOT / 'build/text-golden/TextGolden')
    ap.add_argument('--work-dir', type=pathlib.Path)
    ap.add_argument('--golden', type=pathlib.Path, default=HERE / 'draw.json.gz')
    ap.add_argument('--private-font-dir', type=pathlib.Path)
    args = ap.parse_args()
    if args.private_font_dir and args.golden.resolve().is_relative_to(ROOT):
        ap.error('Private results require an archive outside the repository')
    if args.action == 'capture':
        subprocess.run(['git', 'diff', '--exit-code', baseline.REVISION, '--', 'lib/GfxRenderer',
                        'lib/EpdFont', 'lib/MiniBidi', 'lib/Utf8', 'rust', 'freeink-sdk'],
                       cwd=ROOT, stdout=subprocess.DEVNULL, check=True)
        sdk = subprocess.check_output(['git', '-C', str(ROOT / 'freeink-sdk'), 'rev-parse', 'HEAD'], text=True).strip()
        if sdk != baseline.SDK_REVISION:
            ap.error('Capture requires the pinned SDK')
    start = time.monotonic()
    temporary = tempfile.TemporaryDirectory(prefix='draw-golden-') if not args.work_dir else None
    work = pathlib.Path(temporary.name) if temporary else args.work_dir.resolve()
    if args.private_font_dir and work.is_relative_to(ROOT):
        ap.error('Private results require a working directory outside the repository')
    work.mkdir(parents=True, exist_ok=True)
    _, _, documents, inputs = baseline.corpus()
    fonts, font_inputs = baseline.fonts(work, args.private_font_dir)
    inputs.update(font_inputs)
    expected, counts, excerpts = cases(work, fonts, documents)
    inputs['cases.tsv'] = baseline.sha((work / 'cases.tsv').read_bytes())
    output = work / 'records.tsv'
    with output.open('wb') as raw:
        subprocess.run([str(args.binary.resolve()), 'fonts.tsv', 'cases.tsv'], cwd=work, stdout=raw, check=True)
    groups, seen, samples = {}, {}, {}
    with output.open('rb') as raw:
        for line in raw:
            key = line.split(b'\t', 1)[0].decode('ascii')
            group, index = key.rsplit(':', 1)
            groups.setdefault(group, hashlib.sha256()).update(line)
            seen[group] = seen.get(group, 0) + 1
            if index.startswith('0-'):
                samples[key] = line.decode('ascii').rstrip('\n').split('\t')[1:]
    if seen != expected:
        raise SystemExit('Missing/extra drawing records')
    result = {'format': 1, 'capture_revision': baseline.REVISION, 'sdk_revision': baseline.SDK_REVISION,
              'inputs_sha256': inputs, 'fonts': fonts, 'counts': counts, 'excerpts': excerpts,
              'groups': {key: {'count': seen[key], 'sha256': value.hexdigest()} for key, value in groups.items()},
              'samples': samples}
    data = json.dumps(result, sort_keys=True, ensure_ascii=False, separators=(',', ':')).encode() + b'\n'
    if args.action == 'capture':
        archive = gzip.compress(data, mtime=0)
        binary_bytes = len(archive) + sum(p.stat().st_size for p in HERE.rglob('*.gz') if p.name != 'draw.json.gz')
        if not args.private_font_dir and binary_bytes >= 2_000_000:
            raise SystemExit(f'Combined public binary fixtures exceed 2 MB: {binary_bytes}')
        args.golden.parent.mkdir(parents=True, exist_ok=True)
        args.golden.write_bytes(archive)
    else:
        old = json.loads(gzip.decompress(args.golden.read_bytes()))
        if old != result:
            changed = [key for key in set(old['groups']) | set(result['groups'])
                       if old['groups'].get(key) != result['groups'].get(key)]
            print('Changed groups:', *sorted(changed)[:20], sep='\n')
            (work / 'actual.json.gz').write_bytes(gzip.compress(data, mtime=0))
            raise SystemExit(f'Draw golden mismatch; inspect {work}')
    summary = {'action': args.action, 'elapsed_seconds': round(time.monotonic() - start, 3),
               'records': sum(seen.values()), 'groups': len(groups), 'counts': counts,
               'archive_bytes': args.golden.stat().st_size, 'raw_bytes': output.stat().st_size}
    (work / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
