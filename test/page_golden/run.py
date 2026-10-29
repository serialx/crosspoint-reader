#!/usr/bin/env python3
"""Capture/check every page built with the production pipeline and real fonts."""
import argparse
import gzip
import hashlib
import importlib.util
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import time

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
REVISION = '9738db01'
SDK = '37d272d42732c31065ebed4d9cb90cef7bfa7416'


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


section = load_module('section_oracle', ROOT / 'test/section_golden/run.py')


def sha(data):
    return hashlib.sha256(data).hexdigest()


def encode(value):
    return json.dumps(value, sort_keys=True, ensure_ascii=False, separators=(',', ':')).encode() + b'\n'


def capture_guard():
    subprocess.run(['git', 'diff', '--exit-code', REVISION, '--', 'lib', 'src', 'rust', 'freeink-sdk'],
                   cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
    actual = subprocess.check_output(['git', '-C', str(ROOT / 'freeink-sdk'), 'rev-parse', 'HEAD'], text=True).strip()
    dirty = subprocess.check_output(['git', '-C', str(ROOT / 'freeink-sdk'), 'status', '--porcelain'], text=True)
    if actual != SDK or dirty:
        raise ValueError('Capture requires clean pinned SDK')


def parse_records(path, strips=False):
    pages, chapters, counts = {}, {}, {'pages': 0, 'pixel_cases': 0, 'words': 0, 'footnotes': 0,
                                      'links': 0, 'ruby': 0, 'small_caps': 0, 'focus_words': 0,
                                      'discretionary_hyphens': 0, 'element_tags': {}}
    current = None
    with path.open() as stream:
        for line in stream:
            fields = line.rstrip('\n').split('\t')
            tag = fields[0]
            if tag == 'section':
                chapters[fields[1]] = int(fields[2])
            elif tag == 'begin':
                if current is not None or fields[1] in pages:
                    raise ValueError('Duplicate/nested page')
                current = fields[1]
                record = {'records': [], 'pixels': [], 'calls': []}
            elif tag == 'end':
                if current is None:
                    raise ValueError('Missing page start')
                data = ''.join(record.pop('records')).encode()
                record['records_sha256'] = sha(data)
                record['records_bytes'] = len(data)
                # Full canonical output stays in records.tsv. Keep a bounded readable sample per section.
                if current.endswith('/0'):
                    record['sample'] = data.decode().splitlines()[:40]
                pages[current] = record
                counts['pages'] += 1
                current = None
            elif current is None:
                raise ValueError(f'Unexpected record: {line[:100]}')
            elif tag in ('raw', 'wire'):
                data = bytes.fromhex(fields[1])
                record[tag + '_sha256'] = sha(data)
                record[tag + '_bytes'] = len(data)
            elif tag == 'pixels':
                record['pixels'].append(fields[1:])
                counts['pixel_cases'] += 1
            elif tag == 'cost':
                continue
            elif tag == 'calls':
                record['calls'].append(fields[1])
            else:
                record['records'].append(line)
                if tag == 'word':
                    counts['words'] += 1
                    values = list(map(int, fields[3].split(',')))
                    counts['small_caps'] += values[3] != 0
                    counts['discretionary_hyphens'] += values[4] != 0
                    counts['focus_words'] += values[6] != 0
                elif tag in ('footnote', 'link', 'ruby'):
                    counts[{'footnote': 'footnotes', 'link': 'links', 'ruby': 'ruby'}[tag]] += 1
                elif tag == 'element':
                    tags = counts['element_tags']
                    tags[fields[1]] = tags.get(fields[1], 0) + 1
    expected = {f'{spine}/{page}' for spine, count in chapters.items() for page in range(count)}
    if current is not None or pages.keys() != expected:
        raise ValueError('Missing or extra pages')
    for page in pages.values():
        if len(page['pixels']) not in ((264, 528) if strips else (96, 192)) or len(page['calls']) != len(page['pixels']):
            raise ValueError('Incomplete render matrix')
        if 'raw_sha256' not in page or 'wire_sha256' not in page:
            raise ValueError('Missing page bytes')
    return {'chapters': chapters, 'pages': pages, 'counts': counts}


def without_counters(value):
    if isinstance(value, dict):
        return {key: without_counters(item) for key, item in value.items() if key != 'calls'}
    if isinstance(value, list):
        return [without_counters(item) for item in value]
    return value


def compare(expected, actual):
    # G2/R1 intentionally remove these crossings; their counts are diagnostics, not behavior.
    expected, actual = without_counters(expected), without_counters(actual)
    if expected == actual:
        return
    changed = []
    for key in sorted(expected.get('cases', {}).keys() | actual.get('cases', {}).keys()):
        a, b = expected.get('cases', {}).get(key), actual.get('cases', {}).get(key)
        if a != b:
            changed.append(key)
            if a and b:
                for page in sorted(a['pages'].keys() | b['pages'].keys()):
                    x, y = a['pages'].get(page, {}), b['pages'].get(page, {})
                    if x != y:
                        changed.append(f'  page {page}: ' + ', '.join(k for k in x.keys() | y.keys() if x.get(k) != y.get(k)))
                        break
    raise ValueError('Page golden mismatch: ' + '\n'.join(changed[:20] or ['input/provenance changed']))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('action', choices=('capture', 'check'))
    ap.add_argument('--binary', type=pathlib.Path, default=ROOT / 'build/page-golden/PageGolden')
    ap.add_argument('--golden', type=pathlib.Path, default=HERE / 'golden.json.gz')
    ap.add_argument('--work-dir', type=pathlib.Path)
    ap.add_argument('--private-epub', type=pathlib.Path, action='append', default=[])
    ap.add_argument('--private-font-dir', type=pathlib.Path)
    ap.add_argument('--decoded-images', action='store_true', help='Current full-frame reference with real image decoders')
    ap.add_argument('--strips', action='store_true', help='Assert exact strip/full equality and record costs')
    ap.add_argument('--case', help='Check exactly one case from the frozen archive')
    args = ap.parse_args()
    if args.strips:
        os.environ['XP_STRIP_MATRIX'] = '1'
    if args.decoded_images and not args.strips:
        ap.error('--decoded-images requires --strips')
    if args.strips and args.action != 'check':
        ap.error('Strip mode checks current full frames; it cannot capture goldens')
    if args.case and args.action != 'check':
        ap.error('--case is only for checking, never baseline capture')
    private = bool(args.private_epub or args.private_font_dir)
    if private and (not args.private_epub or not args.private_font_dir):
        ap.error('Private runs need both --private-epub and --private-font-dir')
    if private and args.golden.resolve().is_relative_to(ROOT):
        ap.error('Private archive must be outside the repository')
    if args.action == 'capture':
        capture_guard()
    temporary = tempfile.TemporaryDirectory(prefix='page-golden-') if not args.work_dir else None
    work = pathlib.Path(temporary.name) if temporary else args.work_dir.resolve()
    if private and work.is_relative_to(ROOT):
        ap.error('Private working directory must be outside the repository')
    work.mkdir(parents=True, exist_ok=True)
    start = time.monotonic()
    inputs, cases = {}, {}
    fonts = {}
    if private:
        for path in sorted(args.private_font_dir.rglob('*.cpfont')):
            fonts.setdefault(sha(path.read_bytes()), path)
        if not fonts:
            ap.error('No private .cpfont inputs')
    else:
        path = ROOT / 'test/text_golden/fonts/NotoSans_14.cpfont.gz'
        target = work / 'public.cpfont'
        target.write_bytes(gzip.decompress(path.read_bytes()))
        fonts['public'] = target
        inputs[str(path.relative_to(ROOT))] = sha(path.read_bytes())
    epubs = args.private_epub or sorted((ROOT / 'test/epubs').glob('*.epub'))
    schedules = []
    for i, epub in enumerate(epubs):
        name = 'private-' + sha(epub.read_bytes())[:12] if private else epub.stem
        inputs[str(epub.resolve()) if private else str(epub.relative_to(ROOT))] = sha(epub.read_bytes())
        for font_name, font in fonts.items():
            if private:
                inputs[str(font.resolve())] = sha(font.read_bytes())
            for config in ((3,) if private else range(4)):
                schedules.append((name + '/' + font_name + '/' + str(config), epub, font, config))
    if not private:
        for config in range(4):
            schedules.append(('synthetic/public/' + str(config), None, fonts['public'], config))
        for path in sorted((HERE / 'fixtures').rglob('*')):
            if path.is_file():
                inputs[str(path.relative_to(ROOT))] = sha(path.read_bytes())
    if args.case and args.case not in {case[0] for case in schedules}:
        ap.error('Unknown --case')
    for case_index, (key, epub, font, config) in enumerate(schedules):
        if args.case and key != args.case:
            continue
        destination = work / f'case-{case_index:04}'
        destination.mkdir(exist_ok=True)
        if epub:
            spine = section.prepare(epub, destination)
        else:
            shutil.copytree(HERE / 'fixtures', destination, dirs_exist_ok=True)
            spine = ['chapter.xhtml']
        output = destination / 'records.tsv'
        with output.open('wb') as raw, (destination / 'stderr.log').open('wb') as errors:
            subprocess.run([str(args.binary.resolve()), str(destination), str(config), str(font.resolve())],
                           stdout=raw, stderr=errors, check=True)
        result = parse_records(output, args.strips)
        if len(result['chapters']) != len(spine):
            raise ValueError(f'Missing chapters in {key}')
        result['spine'] = spine
        cases[key] = result
        print(f"{key}: {len(spine)} chapters, {result['counts']['pages']} pages", flush=True)
    result = {'format': 1, 'revision': REVISION, 'sdk': SDK, 'inputs': inputs, 'cases': cases}
    archive = gzip.compress(encode(result), mtime=0)
    if args.action == 'capture':
        if not private and len(archive) >= 1_500_000:
            raise ValueError(f'Public golden too large: {len(archive)}')
        args.golden.parent.mkdir(parents=True, exist_ok=True)
        args.golden.write_bytes(archive)
    else:
        expected = json.loads(gzip.decompress(args.golden.read_bytes()))
        if args.case:
            expected['cases'] = {args.case: expected['cases'][args.case]}
        try:
            if args.strips:
                # Full frames still match frozen pixels; the executable compares every
                # assembled strip byte against its unmodified full-frame draw.
                import copy
                expected = copy.deepcopy(expected)
                actual = copy.deepcopy(result)
                for value in (expected, actual):
                    for case in value['cases'].values():
                        case['counts']['pixel_cases'] = 0
                        for page in case['pages'].values():
                            page['pixels'] = [p for p in page['pixels'] if p[0].split(',')[3] == '0']
                if not args.decoded_images:
                    compare(expected, actual)
                else:
                    # Decoded pixels are outside the placeholder-only frozen archive.
                    # Every assembled buffer is compared byte-for-byte in the executable.
                    if expected['cases'].keys() != actual['cases'].keys():
                        raise ValueError('Missing image schedules')
                    for key in actual['cases']:
                        if expected['cases'][key]['pages'].keys() != actual['cases'][key]['pages'].keys():
                            raise ValueError('Missing image pages')
            else:
                compare(expected, result)
        except ValueError:
            (work / 'actual.json.gz').write_bytes(archive)
            raise
    summary = {'action': args.action, 'elapsed_seconds': round(time.monotonic() - start, 3),
               'archive_bytes': len(archive), 'cases': len(cases),
               'chapters': sum(len(c['chapters']) for c in cases.values()),
               'pages': sum(c['counts']['pages'] for c in cases.values()),
               'pixel_cases': sum(c['counts']['pixel_cases'] for c in cases.values()),
               'raw_bytes': sum(p.stat().st_size for p in work.glob('case-*/records.tsv'))}
    (work / 'summary.json').write_bytes(encode(summary))
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
