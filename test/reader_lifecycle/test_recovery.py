"""Compare real recovery policy and retained callbacks with the immutable native trace."""
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

TEST_NAME = 'reader_recovery'
PRODUCTION = Path('crates/xp-ffi/src/app/epub/recovery.rs')
GZIP_SHA = 'b623932c5812c8b8026aa433bef39987b9f56449486a6afee5edb12e3f6a5934'
TRACE_SHA = '8be7ba135789c7113820501add64c572272f6d9a1ec23c6f222db0217cc1c42c'
RUST_MUTANTS = (
    ('ignore-cancellation', b'if cancelled || unsafe { (self.leaves.wifi_connected)() } == 0 {',
     b'if false || unsafe { (self.leaves.wifi_connected)() } == 0 {'),
    ('sync-timeout', b'(self.leaves.trusted_sync)(5000)', b'(self.leaves.trusted_sync)(4999)'),
    ('save-stale-path', b'(self.leaves.state_set_open_path)(self.path().wire());',
     b'(self.leaves.state_set_open_path)(app_abi::Text::from_bytes(b"original.epub"));'),
)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def replace_once(source, before, after):
    assert source.count(before) == 1, ('replacement must match one site', before)
    return source.replace(before, after, 1)


def generated_i18n_directory(archive):
    release = Path(archive).resolve().parent
    headers = sorted((release / 'build').glob('xp-core-*/out/I18nKeys.h'))
    assert headers, ('missing generated I18nKeys.h for archive', str(archive))
    contents = {path.read_bytes() for path in headers}
    assert len(contents) == 1, ('conflicting generated I18nKeys.h files', [str(path) for path in headers])
    return headers[0].parent


def freeze_original():
    # Preserve the historical capture entry and mutants; ordinary checks never enter it.
    source = subprocess.check_output(
        ['git', 'show', '8d534a99:src/activities/reader/EpubReaderActivity.cpp'], cwd=ROOT, text=True)
    methods = '\n'.join(definition(source, signature) for signature in [
        'bool EpubReaderActivity::handleLoadFailure(', 'void EpubReaderActivity::beginLoanTimeSync(',
        'void EpubReaderActivity::render(',
    ])
    if 'bool EpubReaderActivity::runRecoveryPolicy(' in source:
        methods += '\n' + definition(source, 'bool EpubReaderActivity::runRecoveryPolicy(')
    fixture = (HERE / 'recovery_oracle.cpp').read_text().replace('// RECOVERY_METHODS', methods)
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory)
        def run(text):
            (path / 'recovery.cpp').write_text(text)
            subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-fsanitize=address,undefined',
                            str(path / 'recovery.cpp'), *rust_link_flags(), '-o', str(path / 'recovery')], check=True)
            return subprocess.check_output([str(path / 'recovery')])
        actual = run(fixture)
        golden = HERE / 'recovery.golden.gz'
        golden.write_bytes(gzip.compress(actual, mtime=0))
        for before, after in [('result.isCancelled || WiFi.status()', 'false || WiFi.status()'),
                              ('trustedtime::syncNow(5000)', 'trustedtime::syncNow(4999)'),
                              ('APP_STATE.openEpubPath = bookPath;', 'APP_STATE.openEpubPath = "original.epub";')]:
            wrong = fixture.replace(before, after)
            assert wrong != fixture and run(wrong) != actual
        print(f'recovery: {len(actual.splitlines())} original trace lines')


def project(raw):
    """Lock each message and its option rows without changing original events."""
    lines = raw.splitlines(keepends=True)
    output = []
    groups = 0
    index = 0
    while index < len(lines):
        line = lines[index]
        if line.startswith(b'message '):
            groups += 1
            output.extend((b'lock 1\n', line))
            index += 1
            first = index
            while index < len(lines) and lines[index].startswith(b'option '):
                output.append(lines[index])
                index += 1
            assert 1 <= index - first <= 2, ('unexpected message options', line)
            output.append(b'unlock 1\n')
        else:
            output.append(line)
            index += 1
    assert groups == 1156
    projected = b''.join(output)
    assert len(projected.splitlines()) == 16972 and len(projected) == 229978
    return projected, dict(message_groups=groups)


def main():
    if '--freeze-original' in sys.argv:
        freeze_original()
        return
    golden = HERE / 'recovery.golden.gz'
    compressed = golden.read_bytes()
    raw = gzip.decompress(compressed)
    assert digest(compressed) == GZIP_SHA and digest(raw) == TRACE_SHA
    assert len(raw) == 211482 and len(raw.splitlines()) == 14660
    counts = collections.Counter(line.split(b' ', 1)[0].decode() for line in raw.splitlines())
    assert counts['case'] == counts['handled'] == counts['state'] == 1536
    assert counts['translation'] == 3476 and counts['message'] == counts['update'] == 1156
    assert counts['wifi-new'] == counts['wifi-drop'] == counts['start-child'] == 49
    assert counts['child-dismiss'] == 48 and counts['save'] == counts['restart'] == counts['go'] == 4
    assert counts['lock'] == counts['unlock'] == 2 and counts['base-render'] == 1
    expected, projections = project(raw)
    # Historical recovery composition shell only; full owner entry/render has its own tests.
    # Copied from the pre-retirement source; the policy call binds the actual owner.
    wrapper = """void EpubReaderActivity::render(RenderLock&& lock) {
  if (!renderLoadFailure()) ReaderActivity::render(std::move(lock));
  ModelLock modelLock(*this);
  if (overlay == Overlay::None) publishRustScene({});
}"""
    template = (HERE / 'recovery_oracle.cpp').read_text()
    fixture = replace_once(template, '// RECOVERY_METHODS',
                           '#include "recovery_session_rust.inc"\n' + wrapper).encode()
    production_file = ROOT / 'rust' / PRODUCTION
    production = production_file.read_bytes()
    fixture_hashes = {path.name: digest(path.read_bytes()) for path in (
        HERE / 'recovery_oracle.cpp', HERE / 'recovery_session_rust.inc',
        HERE / 'recovery_session_fixture.h', Path(__file__),
        ROOT / 'rust/crates/xp-ffi/src/app/epub/test_support/recovery.rs')}
    try:
        with tempfile.TemporaryDirectory(prefix='reader-recovery-session-') as directory:
            work = Path(directory)
            baseline_flags = rust_link_flags()

            def run(name, archive=None):
                source_path = work / (name + '.cpp')
                source_path.write_bytes(fixture)
                flags = baseline_flags if archive is None else rust_link_flags(archive=str(archive))
                i18n_directory = generated_i18n_directory(baseline_flags[-1] if archive is None else archive)
                executable = work / name
                subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-O0', '-fsanitize=address,undefined',
                                '-DXP_RECOVERY_SESSION', '-I', str(HERE), '-I', str(i18n_directory),
                                str(source_path), *flags, '-o', str(executable)], check=True)
                return subprocess.run([str(executable)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)

            baseline = run('recovery')
            assert baseline.returncode == 0, baseline.stderr.decode(errors='replace')
            assert baseline.stdout == expected, (digest(baseline.stdout), digest(expected))
            print(json.dumps({
                'test_name': TEST_NAME, 'cases': 1536, 'trace_lines': 14660, 'trace_bytes': 211482,
                'original_gzip_sha256': GZIP_SHA, 'original_trace_sha256': TRACE_SHA,
                'actual_trace_sha256': digest(baseline.stdout), 'event_counts': counts,
                'projected_trace_sha256': digest(expected), 'projection_counts': projections,
                'projected_trace_lines': len(expected.splitlines()), 'projected_trace_bytes': len(expected),
                'projected_event_counts': collections.Counter(
                    line.split(b' ', 1)[0].decode() for line in expected.splitlines()),
                'candidate_source_sha256': digest(production), 'fixture_sha256': fixture_hashes,
                'native_render_wrapper_sha256': digest(wrapper.encode()),
                'render_scope': 'direct Rust recovery helper under historical caller lock; native fallback wrapper retained',
                'policy_replacements': [], 'physical_navigation_hooks': ['finish', 'push', 'reopen', 'update'],
                'resource_lock_contract': '1156 message/option groups now hold ResourceAccess; '
                    'label sink locked, retained choice unlocked; all original events preserved',
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
                result = run(name, target / 'release/libxp_ffi.a')
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
    print(f'{TEST_NAME}: 1536 cases / 14660 original lines / 16972 projected lines; '
          '1156 message lock scopes; three actual Rust mutants rejected')


if __name__ == '__main__':
    main()
