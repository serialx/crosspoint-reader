"""Compare real owner settings and retained popup choices with the immutable trace."""
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
sys.path.insert(0, str(ROOT / 'test'))
from rust_host import rust_link_flags

TEST_NAME = 'reader_settings_parent'
PRODUCTION = Path('crates/xp-ffi/src/app/epub/settings.rs')
GZIP_SHA = '5a6f959b5f4cd4b1f4b54a489718c1c5f106d5371b5c937a6b0b882c9cd10b53'
TRACE_SHA = '71d7af971607c028fd419b213407b234234a3f5a91c351896039ec84a1237c7b'
PHYSICAL_MUTANTS = (
    ('status-height', b'return status;', b'return 0;'),
    ('remember-depth', b'event("remember", lockDepth);', b'event("remember", 0);'),
    ('font-sizes-empty', b'return sizes;', b'return {};'),
)
RUST_MUTANTS = (
    ('orientation-early-return', b'&& self.model(|state| state.orientation) == orientation',
     b'|| self.model(|state| state.orientation) == orientation'),
    ('automatic-turn-duration', b'state.turn_duration_ms = 60000 / rate.expect(',
     b'state.turn_duration_ms = 30000 / rate.expect('),
    ('text-settings-save-omitted', b'unsafe { (self.leaves.settings_save)() };', b'{}'),
    ('duplicate-size-first-match', b'selected = index as i32;', b'selected = index as i32; break;'),
)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def replace_once(source, before, after):
    assert source.count(before) == 1, ('mutation must match one site', before)
    return source.replace(before, after, 1)


def generated_i18n_directory(archive):
    release = Path(archive).resolve().parent
    headers = sorted((release / 'build').glob('xp-core-*/out/I18nKeys.h'))
    assert headers, ('missing generated I18nKeys.h for archive', str(archive))
    contents = {path.read_bytes() for path in headers}
    assert len(contents) == 1, ('conflicting generated I18nKeys.h files', [str(path) for path in headers])
    return headers[0].parent


def main():
    if len(sys.argv) != 1:
        raise SystemExit('Settings goldens are immutable; regeneration is not supported.')
    golden = HERE / 'reader-settings.golden.gz'
    compressed = golden.read_bytes()
    expected = gzip.decompress(compressed)
    assert digest(compressed) == GZIP_SHA and digest(expected) == TRACE_SHA
    assert len(expected) == 415128 and len(expected.splitlines()) == 33032
    counts = collections.Counter(line.split(b' ', 1)[0].decode() for line in expected.splitlines())
    assert counts['case'] == counts['config'] == 2304
    assert counts['remember'] == 568 and counts['initial'] == 256
    assert counts['discard'] == counts['update'] == 544
    assert counts['status-height'] == 128 and counts['progress-height'] == 96 and counts['snap'] == 64
    assert counts['retained-result'] == counts['dismiss-popup'] == counts['paint-popup'] == 320
    assert counts['lock'] == 1696 and counts['owner-lock'] == 240 and counts['unlock'] == 1936
    fixture_source = (HERE / 'reader_settings_oracle.cpp').read_bytes()
    marker = b'#include "settings_session_rust.inc"'
    assert fixture_source.count(marker) == 1
    fixture = fixture_source.replace(marker, (HERE / 'settings_session_rust.inc').read_bytes())
    production_file = ROOT / 'rust' / PRODUCTION
    production = production_file.read_bytes()
    fixture_hashes = {path.name: digest(path.read_bytes()) for path in (
        HERE / 'reader_settings_oracle.cpp', HERE / 'settings_session_rust.inc',
        HERE / 'settings_session_fixture.h', Path(__file__),
        ROOT / 'rust/crates/xp-ffi/src/app/epub/test_support/settings.rs')}
    try:
        with tempfile.TemporaryDirectory(prefix='reader-settings-session-') as directory:
            work = Path(directory)
            baseline_flags = rust_link_flags()

            def run(source, name, archive=None):
                source_path = work / (name + '.cpp')
                source_path.write_bytes(source)
                flags = baseline_flags if archive is None else rust_link_flags(archive=str(archive))
                i18n_directory = generated_i18n_directory(baseline_flags[-1] if archive is None else archive)
                executable = work / name
                subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-O0', '-fsanitize=address,undefined',
                                '-I', str(HERE), '-I', str(i18n_directory), str(source_path), *flags,
                                '-o', str(executable)], check=True)
                return subprocess.run([str(executable)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)

            baseline = run(fixture, 'settings')
            assert baseline.returncode == 0, baseline.stderr.decode(errors='replace')
            assert baseline.stdout == expected, (digest(baseline.stdout), TRACE_SHA)
            print(json.dumps({
                'test_name': TEST_NAME, 'cases': 2304, 'trace_lines': 33032, 'trace_bytes': 415128,
                'original_gzip_sha256': GZIP_SHA, 'original_trace_sha256': TRACE_SHA,
                'actual_trace_sha256': digest(baseline.stdout), 'event_counts': counts,
                'candidate_source_sha256': digest(production), 'fixture_sha256': fixture_hashes,
                'lock_label_projection': 'typed orientation entry uses owner-lock; native lock identity is unchanged',
                'policy_replacements': ['initial', 'remember', 'discard', 'paint'],
                'observers': ['status_height', 'progress_height', 'snap'],
            }), flush=True)
            for name, before, after in PHYSICAL_MUTANTS:
                changed = replace_once(fixture, before, after)
                result = run(changed, name)
                assert result.returncode != 0 or result.stdout != expected, (name, 'physical mutant survived')
                print(json.dumps({
                    'test_name': TEST_NAME, 'mutant': name, 'kind': 'physical-fixture', 'replacement_count': 1,
                    'original_source_sha256': digest(fixture), 'mutant_source_sha256': digest(changed),
                    'returncode': result.returncode, 'trace_sha256': digest(result.stdout),
                    'stderr_sha256': digest(result.stderr),
                }), flush=True)

            copied_repo = work / 'source'
            copied_rust = copied_repo / 'rust'
            shutil.copytree(ROOT / 'rust', copied_rust, ignore=shutil.ignore_patterns('target', '__pycache__'))
            for name in ('scripts', 'lib', 'src'):
                (copied_repo / name).symlink_to(ROOT / name, target_is_directory=True)
            copied_source = copied_rust / PRODUCTION
            assert copied_source.read_bytes() == production
            target = work / 'target'
            for name, before, after in RUST_MUTANTS:
                changed = replace_once(production, before, after)
                copied_source.write_bytes(changed)
                subprocess.run(['cargo', 'build', '--manifest-path', str(copied_rust / 'Cargo.toml'),
                                '--target-dir', str(target), '-p', 'xp-ffi', '--release', '--locked',
                                '--features', 'app-test'], cwd=copied_rust, check=True)
                result = run(fixture, name, target / 'release/libxp_ffi.a')
                assert result.returncode != 0 or result.stdout != expected, (name, 'actual Rust mutant survived')
                print(json.dumps({
                    'test_name': TEST_NAME, 'mutant': name, 'kind': 'production-rust', 'replacement_count': 1,
                    'source': str(PRODUCTION), 'original_source_sha256': digest(production),
                    'mutant_source_sha256': digest(changed), 'returncode': result.returncode,
                    'trace_sha256': digest(result.stdout), 'stderr_sha256': digest(result.stderr),
                }), flush=True)
    finally:
        assert golden.read_bytes() == compressed, 'immutable original golden changed'
        assert production_file.read_bytes() == production, 'real production source changed'
    print(f'{TEST_NAME}: 2304 cases / 33032 exact lines; three physical and four actual Rust mutants rejected')


if __name__ == '__main__':
    main()
