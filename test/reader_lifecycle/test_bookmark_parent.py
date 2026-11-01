"""Real bookmark owner versus immutable native traces with the approved lock projection."""
from pathlib import Path
import gzip
import hashlib
import json
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'test'))
from rust_host import rust_link_flags

release = '--release-log' in sys.argv
section_drop = '--section-drop' in sys.argv
if '--freeze-original' in sys.argv:
    raise SystemExit('Bookmark goldens are immutable; regeneration is not supported.')
assert not (release and section_drop)
TEST_NAME = ('reader_bookmark_section_drop' if section_drop else
             'reader_bookmark_parent_release' if release else 'reader_bookmark_parent')
EXPECTED = {
    False: ('4ae165033e0744d93e72eb0f23d28fbbbd33c6820d15f88b92edae2cab4760f2',
            '38078cdbdbdfcda671a409571c9a027c43f1150ce8a57e14b80615f2dc9d80cc'),
    True: ('babe676abb9a9c5616d0f45f9e96c9ebb07883754d92c21ee702e29ea84a1908',
           'b9156b0db97f3fe5b26110d2dbcd8a782735f5e7b40e2255ee9434076b831ea2'),
}
LOCK, UNLOCK = 'lock 1 0', 'unlock 1 0'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def project(original):
    """Insert only coordinator-approved production scopes; keep harness observations verbatim.

    Text/offset now borrow Section while render-locked. Retain mutates the vector
    after its progress range is computed. Insert follows entry construction and
    offset retrieval. Clear and optional native load share one mutation scope;
    the subsequent flag calculation remains outside it. No matching policy runs here.
    """
    lines = original.decode().splitlines()
    starts = [i for i, line in enumerate(lines) if line.startswith('case ')]
    assert len(starts) == 2560 and starts[0] == 0
    output = []
    cursor = 0
    counts = dict(text=0, offset=0, retain=0, insert=0, load=0, clear_only=0)
    for start in starts:
        assert start == cursor
        end = next(i for i in range(start + 1, len(lines)) if lines[i].startswith('loaded ')) + 1
        block = lines[start:end]
        before = {}
        def at(index, *events):
            before.setdefault(index, []).extend(events)
        result = [i for i, line in enumerate(block) if line.startswith('result ')]
        assert len(result) == 1
        result = result[0]
        maps = [i for i, line in enumerate(block) if line == 'map 0 0']
        assert len(maps) <= 1
        if maps:
            range_end = maps[0] + 2
            assert all(line.startswith('progress ') for line in block[maps[0]+1:range_end+1])
            at(range_end + 1, LOCK, UNLOCK)
            counts['retain'] += 1
            sanitizes = [i for i, line in enumerate(block[:result]) if line.startswith('sanitize [')]
            assert len(sanitizes) <= 1
            if sanitizes:
                save = [i for i, line in enumerate(block[:result]) if line.startswith('save [')]
                assert len(save) == 1 and block[save[0]-1] == 'path 0 0'
                at(save[0]-1, LOCK, UNLOCK)
                counts['insert'] += 1
        loads = [i for i, line in enumerate(block[result+1:], result+1) if line.startswith('load [')]
        assert len(loads) <= 1
        if loads:
            assert block[result+1] == 'path 0 0' and loads[0] == result + 2
            at(result+1, LOCK)
            at(loads[0]+1, UNLOCK)
            counts['load'] += 1
        else:
            at(result+1, LOCK, UNLOCK)
            counts['clear_only'] += 1
        for index, line in enumerate(block):
            output.extend(before.get(index, []))
            match = re.fullmatch(r'(text|offset) (-?\d+) 0', line)
            if match:
                counts[match[1]] += 1
                output.extend((LOCK, f'{match[1]} {match[2]} 1', UNLOCK))
            else:
                output.append(line)
        cursor = end
    output.extend(lines[cursor:])
    assert counts == dict(text=136, offset=88, retain=640, insert=320, load=1280, clear_only=1280), counts
    projected = ('\n'.join(output) + '\n').encode()
    assert len(output) == len(lines) + 2 * sum(counts.values())
    return projected, counts


def replace_once(source, before, after):
    assert source.count(before) == 1, f'mutant must match one production site: {before!r}'
    return source.replace(before, after, 1)


def omit_lock(source, following):
    pattern = rb'let (?:_access|access) = self\.lock_resources\(\);(?P<gap>\s*)(?P<next>' + following + rb')'
    matches = list(re.finditer(pattern, source))
    assert len(matches) == 1, f'lock mutant must match one scope: {following!r}'
    match = matches[0]
    original = match[0]
    changed = original.replace(b'self.lock_resources()', b'unsafe { self.borrowed_resources() }', 1)
    return source[:match.start()] + changed + source[match.end():]


BOOKMARK = Path('crates/xp-ffi/src/app/epub/bookmarks.rs')
STORAGE = Path('crates/xp-ffi/src/app/epub/storage.rs')
MATH = Path('crates/xp-ui/src/reader/bookmark.rs')
text_omission = lambda source: omit_lock(source, rb'let Some\(text\) = self\.section_text\(&access, page\)')
mutations = [
    ('visible-offset-ignored', BOOKMARK, lambda s: replace_once(s,
        b'let mut offset = self.model(|state| state.visible_offset);', b'let mut offset = None;')),
    ('consumed-links-retained', STORAGE, lambda s: replace_once(s,
        b'(self.leaves.path_remove)(path.wire())', b'0u8')),
    ('text-lock-omitted', BOOKMARK, text_omission),
    ('insert-lock-omitted', BOOKMARK, lambda s: omit_lock(s,
        rb'if unsafe \{\s*\(self\.leaves\.bookmarks_insert_front\)')),
    ('retain-predicate-inverted', BOOKMARK, lambda s: replace_once(s,
        b'u8::from(!predicate.matches(&*row))', b'u8::from(predicate.matches(&*row))')),
    ('epsilon-zero', MATH, lambda s: replace_once(s,
        b'progress + 0.0001 >= range.start && progress - 0.0001 <= range.end',
        b'progress + 0.0 >= range.start && progress - 0.0 <= range.end')),
]
if section_drop:
    mutations = [('text-lock-omitted', BOOKMARK, text_omission)]
if release:
    mutations = []

fixture = (HERE / 'bookmark_parent_oracle.cpp').read_text().replace(
    '#include "bookmark_session_rust.inc"', (HERE / 'bookmark_session_rust.inc').read_text())
golden = HERE / f'bookmark-parent{"-release" if release else ""}.golden.gz'
compressed = golden.read_bytes()
original = gzip.decompress(compressed)
assert (digest(compressed), digest(original)) == EXPECTED[release]
case_counts = {name: sum(line.startswith(prefix) for line in original.splitlines())
               for name, prefix in [('bookmark', b'case '), ('range', b'range '),
                                    ('matches', b'matches '), ('edge', b'edge '), ('link_depth', b'depth ')]}
assert case_counts == dict(bookmark=2560, range=36, matches=864, edge=704, link_depth=392)
projected, projection_counts = project(original)
production = {path: (ROOT / 'rust' / path).read_bytes() for path in (BOOKMARK, STORAGE, MATH)}
with tempfile.TemporaryDirectory(prefix='reader-bookmarks-') as directory:
    work = Path(directory)
    cpp = work / 'bookmark.cpp'
    cpp.write_text(fixture)
    baseline_flags = rust_link_flags()
    def run(name, archive=None):
        flags = baseline_flags if archive is None else rust_link_flags(archive=str(archive))
        subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-fsanitize=address,undefined',
                        '-I', str(ROOT / 'src'), '-I', str(HERE), '-I', str(HERE / 'stubs'),
                        *(['-DBOOKMARK_RELEASE_LOG'] if release else []), str(cpp), *flags,
                        '-o', str(work / name)], check=True)
        return subprocess.run([str(work / name), *(['--section-drop'] if section_drop else [])],
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    baseline = run('bookmark')
    assert baseline.returncode == 0, baseline.stderr.decode(errors='replace')
    actual = baseline.stdout
    if section_drop:
        assert actual.endswith(b'section-drop summary-empty offset-absent saved=1\n')
        assert b'sanitize []\n' in actual and b'\ntext ' not in actual and b'\noffset ' not in actual
        assert b'save [/fixture.epub] 1\n' in actual
    else:
        assert actual == projected, (TEST_NAME, digest(actual), digest(projected))
    print(json.dumps({'test_name': TEST_NAME, 'release': release, 'section_drop': section_drop,
                      'original_gzip_sha256': digest(compressed), 'original_trace_sha256': digest(original),
                      'projected_trace_sha256': digest(projected), 'actual_trace_sha256': digest(actual),
                      'original_lines': len(original.splitlines()), 'projected_lines': len(projected.splitlines()),
                      'projection_event_counts': projection_counts,
                      'original_case_counts': case_counts,
                      'candidate_harness_sha256': digest(fixture.encode())}), flush=True)
    if mutations:
        copied_repo = work / 'source'
        copied_rust = copied_repo / 'rust'
        shutil.copytree(ROOT / 'rust', copied_rust, ignore=shutil.ignore_patterns('target', '__pycache__'))
        for name in ('scripts', 'lib', 'src'):
            (copied_repo / name).symlink_to(ROOT / name, target_is_directory=True)
        target = work / 'target'
        for name, path, mutate in mutations:
            for source_path, data in production.items():
                (copied_rust / source_path).write_bytes(data)
            wrong = mutate(production[path])
            assert wrong != production[path]
            (copied_rust / path).write_bytes(wrong)
            subprocess.run(['cargo', 'build', '--manifest-path', str(copied_rust / 'Cargo.toml'),
                            '--target-dir', str(target), '-p', 'xp-ffi', '--release', '--locked',
                            '--features', 'app-test'], cwd=copied_rust, check=True)
            result = run(name, target / 'release/libxp_ffi.a')
            assert result.returncode != 0 or result.stdout != actual, f'mutant escaped: {name}'
            print(json.dumps({'test_name': TEST_NAME, 'mutant': name, 'replacement_count': 1,
                              'source': str(path), 'original_source_sha256': digest(production[path]),
                              'mutant_source_sha256': digest(wrong), 'returncode': result.returncode,
                              'trace_sha256': digest(result.stdout), 'stderr_sha256': digest(result.stderr)}), flush=True)
    assert golden.read_bytes() == compressed
    for path, data in production.items():
        assert (ROOT / 'rust' / path).read_bytes() == data
    print(f'{TEST_NAME}: exact approved trace projection; {len(mutations)} actual Rust mutants rejected')
