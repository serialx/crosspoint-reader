#!/usr/bin/env python3
"""Capture/check the production text renderer against the frozen C++ baseline."""
import argparse
import gzip
import hashlib
import html.parser
import itertools
import json
import pathlib
import re
import subprocess
import tempfile
import time
import zipfile

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
REVISION = 'c9330408c0ff5fd22a95d0dbb635b1ff5ffa5dca'
SDK_REVISION = '37d272d42732c31065ebed4d9cb90cef7bfa7416'
BUILTINS = {'noto12': 1597191560, 'noto14': -1413326613, 'noto16': 116566294,
            'noto18': -348426591, 'ubuntu8': -1769521860, 'ubuntu10': 1322569422,
            'ubuntu12': 1831230762}
STYLES = [base | overlay for overlay in (0, 16, 32, 128) for base in range(4)]
SYNTHETIC = {
    'empty': '', 'one': 'A', 'space': ' ', 'kerning': 'AVATAR To WA Yo',
    'ligatures': 'office ffi ffl fi fl ff', 'latin': 'café naïve Straße Æsir ōlelo',
    'korean': '한글 읽기 가나다 각힣', 'jamo': '각 ㄱㅏ',
    'chinese': '中文阅读，天地玄黄。', 'japanese': '日本語 カタカナ ひらがな',
    'arabic': 'السلام عليكم', 'lamalef': 'لا لأ لإ لآ بلا الله',
    'harakat': 'بِسْمِ اللَّهِ الرَّحْمَٰنِ', 'persian': 'کسالت فارسی',
    'hebrew': 'שָׁלוֹם בּוֹ שִׂ', 'mixed': 'abc שלום 123 العربية (xyz)',
    'combining': 'A\u0301 e\u0308\u0323 o\u0302\u0301', 'orphan': '\u0301\u05bc\u064e',
    'solid': '█■▉□', 'smallcaps': 'Abc xyz éßﬃ', 'supsub': 'H2O x2 123',
    'spaces': 'A V\u00a0T\u3000W', 'missing': '😀\U0001f9d1\U0010ffff',
    'controls': 'a\u200db\u200cc\u202bRTL\u202c',
    'invalid-lead': bytes.fromhex('fffe418042'),
    'invalid-short': bytes.fromhex('41e282'),
    'invalid-overlong': bytes.fromhex('c0afeda080f4908080'),
}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def packed(value):
    return value if isinstance(value, bytes) else value.encode('utf8')


class Text(html.parser.HTMLParser):
    """Simple body tokenizer: block boundaries are spaces; inline text stays joined."""
    blocks = {'p', 'div', 'br', 'hr', 'li', 'tr', 'td', 'th', 'h1', 'h2', 'h3',
              'h4', 'h5', 'h6', 'section', 'article', 'blockquote', 'pre'}

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.parts = []
        self.body = False
        self.skip = 0

    def handle_starttag(self, tag, attrs):
        if tag == 'body':
            self.body = True
        if tag in ('script', 'style'):
            self.skip += 1
        if self.body and tag in self.blocks:
            self.parts.append(' ')

    def handle_endtag(self, tag):
        if tag in ('script', 'style'):
            self.skip -= 1
        if self.body and tag in self.blocks:
            self.parts.append(' ')
        if tag == 'body':
            self.body = False

    def handle_data(self, data):
        if self.body and not self.skip:
            self.parts.append(data)


def corpus():
    documents, words, contexts, files = {}, set(), set(), {}
    for epub in sorted((ROOT / 'test/epubs').glob('*.epub')):
        files[str(epub.relative_to(ROOT))] = sha(epub.read_bytes())
        with zipfile.ZipFile(epub) as archive:
            for name in sorted(archive.namelist()):
                if pathlib.Path(name).suffix.lower() not in ('.html', '.xhtml', '.htm'):
                    continue
                parser = Text()
                parser.feed(archive.read(name).decode('utf8'))
                tokens = re.findall(r'\S+', ''.join(parser.parts))
                documents[epub.name + '/' + name] = tokens
                words.update(tokens)
                contexts.update((ord(a[-1]), ord(b[0])) for a, b in zip(tokens, tokens[1:]))
    return sorted(words), sorted(contexts), documents, files


def fonts(work, private):
    result = dict(BUILTINS)
    inventory = {}
    lines = []
    paths = []
    for path in sorted((ROOT / 'lib/EpdFont/builtinFonts').glob('*.h')):
        inventory[str(path.relative_to(ROOT))] = sha(path.read_bytes())
    for index, path in enumerate(sorted((HERE / 'fonts').glob('*.cpfont.gz'))):
        paths.append(('S', 100 + index, 'sd-' + path.name.removesuffix('.cpfont.gz'), path))
    for index, family in enumerate(('NotoSans', 'NotoSansArabic', 'NotoSansHebrew')):
        path = ROOT / f'lib/EpdFont/builtinFonts/source/{family}/{family}-Regular.ttf'
        paths.append(('T', 200 + index, 'ttf-' + family, path))
    if private:
        found = sorted(private.rglob('*.cpfont'))
        if not found:
            raise SystemExit(f'No .cpfont files in {private}')
        # Identical copies across card fixtures are one font input.
        unique = {}
        for path in found:
            unique.setdefault(sha(path.read_bytes()), path)
        for index, (digest, path) in enumerate(sorted(unique.items())):
            paths.append(('S', 1000 + index, 'private-' + digest[:12], path))
    for kind, id_, name, path in paths:
        link = work / f'font-{id_}{".cpfont" if kind == "S" else ".ttf"}'
        if link.is_symlink():
            link.unlink()
        if path.suffix == '.gz':
            link.write_bytes(gzip.decompress(path.read_bytes()))
        else:
            if link.exists():
                link.unlink()
            link.symlink_to(path)
        lines.append(f'{kind} {id_} {link.name.encode().hex()}\n')
        result[name] = id_
        key = str(path.relative_to(ROOT)) if path.is_relative_to(ROOT) else str(path)
        inventory[key] = sha(path.read_bytes())
    (work / 'fonts.tsv').write_text(''.join(lines))
    if private:
        result = {name: id_ for name, id_ in result.items() if name.startswith('private-')}
    return result, inventory


def cases(work, all_fonts, words, contexts):
    counts = {'synthetic': 0, 'words': 0, 'spaces': 0, 'fallback': 0, 'draws': 0}
    groups = {}
    # A group names its configuration; each indexed record is a corpus member.
    with (work / 'cases.tsv').open('w') as output:
        def emit(group, index, font, style, tracking, direction, mode, draw, fallback, left, right, text):
            data = packed(text).hex() or '-'
            output.write(f'{group}:{index} {font} {style} {tracking} {direction} {mode} '
                         f'{draw} {fallback} {left} {right} {data}\n')
            groups[group] = groups.get(group, 0) + 1
            counts['draws'] += draw

        for name, font in all_fonts.items():
            for style, tracking, direction, mode in itertools.product(STYLES, range(-2, 5), (-1, 0, 1), (0, 1)):
                group = f'synthetic/{name}/{style}/{tracking}/{direction}/{mode}'
                for label, text in SYNTHETIC.items():
                    draw = int(mode == 1 and tracking in (-2, 0, 4))
                    emit(group, label, font, style, tracking, direction, mode, draw, 0, 65, 86, text)
                    counts['synthetic'] += 1
            # Layout callbacks default to AUTO + Layout; the full tracking matrix is above.
            for style, tracking in itertools.product(STYLES, (0,)):
                group = f'words/{name}/{style}/{tracking}/-1/0'
                for i, word in enumerate(words):
                    emit(group, i, font, style, tracking, -1, 0, 0, 0, 65, 86, word)
                    counts['words'] += 1
            for style in STYLES:
                group = f'spaces/{name}/{style}/0/-1/0'
                for i, (left, right) in enumerate(contexts):
                    emit(group, i, font, style, 0, -1, 0, 0, 0, left, right, ' ')
                    counts['spaces'] += 1
        for name, font in all_fonts.items():
            if not name.startswith(('sd-', 'private-')):
                continue
            for style, direction in itertools.product(STYLES, (-1, 0, 1)):
                group = f'fallback/{name}/{style}/0/{direction}/1'
                for label, text in SYNTHETIC.items():
                    emit(group, label, BUILTINS['ubuntu10'], style, 0, direction, 1, 1, font, 65, 86, text)
                    counts['fallback'] += 1
    return counts, groups


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('action', choices=('capture', 'check'))
    ap.add_argument('--binary', type=pathlib.Path, default=ROOT / 'build/text-golden/TextGolden')
    ap.add_argument('--work-dir', type=pathlib.Path)
    ap.add_argument('--golden', type=pathlib.Path, default=HERE / 'golden.json.gz')
    ap.add_argument('--private-font-dir', type=pathlib.Path)
    args = ap.parse_args()
    if args.private_font_dir and args.golden.resolve().is_relative_to(ROOT):
        ap.error('Private captures/checks require --golden outside the repository')
    if args.action == 'capture':
        watched = ['lib/GfxRenderer', 'lib/EpdFont', 'lib/MiniBidi', 'lib/Utf8', 'rust', 'freeink-sdk']
        subprocess.run(['git', 'diff', '--exit-code', REVISION, '--', *watched], cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
        sdk = subprocess.check_output(['git', '-C', str(ROOT / 'freeink-sdk'), 'rev-parse', 'HEAD'], text=True).strip()
        if sdk != SDK_REVISION:
            raise SystemExit('Capture requires the pinned SDK')
    start = time.monotonic()
    temporary = tempfile.TemporaryDirectory(prefix='text-golden-') if not args.work_dir else None
    work = pathlib.Path(temporary.name) if temporary else args.work_dir.resolve()
    if args.private_font_dir and work.resolve().is_relative_to(ROOT):
        ap.error('Private runs require --work-dir outside the repository')
    work.mkdir(parents=True, exist_ok=True)
    words, contexts, documents, inputs = corpus()
    all_fonts, font_inputs = fonts(work, args.private_font_dir)
    inputs.update(font_inputs)
    inputs['test/text_golden/inventory.json'] = sha((HERE / 'inventory.json').read_bytes())
    counts, expected_groups = cases(work, all_fonts, words, contexts)
    (work / 'corpus.json').write_text(json.dumps({'documents': documents, 'words': words, 'contexts': contexts},
                                               ensure_ascii=False, indent=2) + '\n')
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
            # Detailed glyph traces/pixel hashes for every script/style/font/base direction at tracking 0.
            fields = group.split('/')
            if fields[0] in ('synthetic', 'fallback') and fields[3] == '0' and fields[-1] == '1':
                samples[key] = line.decode('ascii').rstrip('\n').split('\t')[1:]
    if seen != expected_groups:
        raise SystemExit('Missing/extra output records')
    result = {'format': 1, 'capture_revision': REVISION, 'sdk_revision': SDK_REVISION,
              'inputs_sha256': inputs, 'fonts': all_fonts, 'counts': counts,
              'corpus': {'documents': len(documents), 'word_occurrences': sum(map(len, documents.values())),
                         'unique_words': len(words), 'space_contexts': len(contexts),
                         'sha256': sha((work / 'corpus.json').read_bytes())},
              'groups': {key: {'count': seen[key], 'sha256': value.hexdigest()} for key, value in groups.items()},
              'samples': samples}
    data = json.dumps(result, sort_keys=True, ensure_ascii=False, separators=(',', ':')).encode() + b'\n'
    if args.action == 'capture':
        args.golden.parent.mkdir(parents=True, exist_ok=True)
        archive = gzip.compress(data, mtime=0)
        if not args.private_font_dir and len(archive) >= 1_500_000:
            raise SystemExit(f'Public golden exceeds 1.5 MB: {len(archive)} bytes')
        args.golden.write_bytes(archive)
    else:
        old = json.loads(gzip.decompress(args.golden.read_bytes()))
        if old != result:
            changed = [key for key in set(old['groups']) | set(result['groups'])
                       if old['groups'].get(key) != result['groups'].get(key)]
            print('Changed groups:', *sorted(changed)[:20], sep='\n')
            (work / 'actual.json.gz').write_bytes(gzip.compress(data, mtime=0))
            raise SystemExit(f'Text golden mismatch; inspect {work}')
    summary = {'action': args.action, 'elapsed_seconds': round(time.monotonic() - start, 3),
               'archive_bytes': args.golden.stat().st_size, 'records': sum(seen.values()),
               'raw_bytes': output.stat().st_size, 'groups': len(groups), 'samples': len(samples),
               'corpus': result['corpus'], 'counts': counts}
    print(json.dumps(summary, indent=2))
    (work / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')


if __name__ == '__main__':
    main()
