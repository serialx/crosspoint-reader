"""Compare the ordinary EPUB owner with an immutable, explicitly locked trace."""
import collections
import gzip
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from extract import definition
sys.path.insert(0, str(ROOT / 'test'))
from rust_host import rust_link_flags

PRODUCTION = Path('crates/xp-ffi/src/app/epub/overlay_actions.rs')


TEST_NAME = 'reader_close_overlay'
CASES = 48
ORACLE = 'close_overlay_oracle.cpp'
ADAPTER = 'close_overlay_session_rust.inc'
GOLDEN = 'close-overlay.golden.gz'
GZIP_SHA = '589140a8c835a4ed6d0c0086ceb901d6cced14061c6e7f830b6d0d249c03c0d7'
MARKER = '// CLOSE_OVERLAY_METHOD'
SIGNATURE = 'void EpubReaderActivity::closeOverlayToPage()'
MUTANT = 'a4-counter-zero'
MUTANT_BEFORE = b'self.set_refresh_counter(1);'
MUTANT_AFTER = b'self.set_refresh_counter(0);'
CONTRACT = '24 toolbar drops and 36 discards acquire nonrecursive render lock; counter events read actual owner observations'


def project(raw):
    output = []
    cases = drops = discards = 0
    for line in raw.splitlines(keepends=True):
        if line.startswith(b'case='):
            cases += 1
        if line.startswith((b'toolbar-drop ', b'discard ')):
            name, suffix = line.split(b' ', 1)
            assert suffix.startswith(b'counter=') and suffix.endswith(b'\n'), line
            if name == b'toolbar-drop':
                drops += 1
            else:
                discards += 1
            output.extend((b'lock ' + suffix, line, b'unlock ' + suffix))
        else:
            output.append(line)
    # Source matrix: 2 A4 x2 panel x2 stored x2 toolbar x3 counters.
    # Direct restoration uses 2 x1(non-Xteink) x1(stored) x2 x3 =12 cases.
    assert cases == 48 and drops == 24 and discards == 36
    projected = b''.join(output)
    assert len(projected.splitlines()) == len(raw.splitlines()) + 120
    return projected, dict(toolbar_drop=drops, discard=discards)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def replace_once(source, before, after):
    assert source.count(before) == 1, ('expected one replacement', before)
    return source.replace(before, after, 1)


def freeze_original():
    source = subprocess.check_output(
        ['git', 'show', '8d534a99:src/activities/reader/EpubReaderActivity.cpp'], cwd=ROOT, text=True)
    fixture = (HERE / ORACLE).read_text().replace(MARKER, definition(source, SIGNATURE))
    with tempfile.TemporaryDirectory() as directory:
        work = Path(directory)
        path = work / 'original.cpp'
        path.write_text(fixture)
        binary = work / 'original'
        subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-fsanitize=address,undefined', str(path),
                        *rust_link_flags(), '-o', str(binary)], check=True)
        actual = subprocess.check_output([str(binary)])
        (HERE / GOLDEN).write_bytes(gzip.compress(actual, mtime=0))
        print(f'{TEST_NAME}: {len(actual.splitlines())} original capture lines')


def main():
    if sys.argv[1:] == ['--freeze-original']:
        freeze_original()
        return
    if len(sys.argv) != 1:
        raise SystemExit('Only --freeze-original may regenerate the historical capture.')
    golden = HERE / GOLDEN
    compressed = golden.read_bytes()
    assert digest(compressed) == GZIP_SHA
    raw = gzip.decompress(compressed)
    expected, projections = project(raw)
    template = (HERE / ORACLE).read_bytes()
    fixture = replace_once(template, MARKER.encode(), (HERE / ADAPTER).read_bytes())
    production_file = ROOT / 'rust' / PRODUCTION
    production = production_file.read_bytes()
    fixture_hashes = {name: digest((HERE / name).read_bytes()) for name in
                      (ORACLE, ADAPTER, 'overlay_actions_session_fixture.h', Path(__file__).name)}
    fixture_hashes['overlay_actions.rs'] = digest(
        (ROOT / 'rust/crates/xp-ffi/src/app/epub/test_support/overlay_actions.rs').read_bytes())
    try:
        with tempfile.TemporaryDirectory(prefix='reader-overlay-actions-') as directory:
            work = Path(directory)
            flags = rust_link_flags()

            def run(name, archive=None):
                path = work / (name + '.cpp')
                path.write_bytes(fixture)
                binary = work / name
                selected_flags = flags if archive is None else rust_link_flags(archive=str(archive))
                subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-O0', '-fsanitize=address,undefined',
                                '-DXP_OVERLAY_SESSION', '-I', str(HERE), str(path), *selected_flags,
                                '-o', str(binary)], check=True)
                return subprocess.run([str(binary)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)

            baseline = run('baseline')
            assert baseline.returncode == 0, baseline.stderr.decode(errors='replace')
            assert baseline.stdout == expected, (digest(baseline.stdout), digest(expected))
            print(json.dumps({
                'test_name': TEST_NAME, 'cases': CASES,
                'original_gzip_sha256': GZIP_SHA, 'original_trace_sha256': digest(raw),
                'original_trace_lines': len(raw.splitlines()), 'original_trace_bytes': len(raw),
                'projected_trace_sha256': digest(expected), 'projected_trace_lines': len(expected.splitlines()),
                'projected_trace_bytes': len(expected), 'actual_trace_sha256': digest(baseline.stdout),
                'projection_counts': projections, 'resource_contract': CONTRACT,
                'original_event_counts': collections.Counter(line.split(b' ', 1)[0].decode() for line in raw.splitlines()),
                'candidate_source_sha256': digest(production), 'fixture_sha256': fixture_hashes,
                'policy': 'ordinary owner methods; typed historical hooks; no fixture decision dispatcher',
            }), flush=True)
            copied_repo = work / 'source'
            copied_rust = copied_repo / 'rust'
            shutil.copytree(ROOT / 'rust', copied_rust, ignore=shutil.ignore_patterns('target', '__pycache__'))
            for name in ('scripts', 'lib', 'src'):
                (copied_repo / name).symlink_to(ROOT / name, target_is_directory=True)
            changed = replace_once(production, MUTANT_BEFORE, MUTANT_AFTER)
            (copied_rust / PRODUCTION).write_bytes(changed)
            target = work / 'target'
            subprocess.run(['cargo', 'build', '--manifest-path', str(copied_rust / 'Cargo.toml'),
                            '--target-dir', str(target), '-p', 'xp-ffi', '--release', '--locked',
                            '--features', 'app-test'], cwd=copied_rust, check=True)
            result = run('mutant', target / 'release/libxp_ffi.a')
            assert result.returncode != 0 or result.stdout != expected, 'actual Rust mutant survived'
            print(json.dumps({
                'test_name': TEST_NAME, 'mutant': MUTANT, 'kind': 'production-rust', 'replacement_count': 1,
                'source': str(PRODUCTION), 'original_source_sha256': digest(production),
                'mutant_source_sha256': digest(changed), 'returncode': result.returncode,
                'trace_sha256': digest(result.stdout), 'stderr_sha256': digest(result.stderr),
            }), flush=True)
    finally:
        assert golden.read_bytes() == compressed, 'immutable original golden changed'
        assert production_file.read_bytes() == production, 'real production source changed'
    print(f'{TEST_NAME}: {CASES} cases; exact approved lock projection; actual Rust mutant rejected')


if __name__ == '__main__':
    main()
