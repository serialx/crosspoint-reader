"""Exercise real EpubSession render composition and extracted native frame ownership."""
import argparse
import collections
import gzip
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from test_popup_resource import definition, native_leaf, replace_once

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'test'))
from rust_host import rust_link_flags

PRODUCTION = Path('crates/xp-ffi/src/app/epub/render.rs')
NATIVE = Path('src/activities/reader/RustEpubResources.cpp')
GOLDENS = {
    'off': (203194, 'a933b975252875fa1e7d5b26f72b24277a55e70ad360f25161bb7dc3d613aeba',
            'b7ab32a95057bbb39f4e7a8ab4fc6e3cf58cd361ab24cad50264c1fb7489f107'),
    'on': (251550, 'd94727da9f014a61ca6721df83d7a81336215eee12486549626d536552005ab2',
           '56dcfdb47b7ad4f42eff66ae5f86f71c94ef266e4249c0b383ae65b9829874cf'),
}
RUST_MUTANTS = (
    ('forced-refresh-not-consumed', 'u32::from(owner.take_forced_refresh(self.access))', '0', False),
    ('counter-publication', 'value as i32', '(value as i32).wrapping_add(1)', False),
    ('status-not-intercepted', 'Command::Status =>', 'Command::Status if false =>', False),
    ('highlight-coordinates', 'self.highlights(self.page, self.font, self.left, self.top);',
     'self.highlights(self.page, self.font, self.top, self.left);', False),
    ('page-before-finish', '        drop(call);\n        drop(page);',
     '        drop(page);\n        drop(call);', True),
)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def fixture(native):
    source = (HERE / 'render_oracle.cpp').read_text()
    source = replace_once(source, '// ACTUAL_NATIVE_FRAME', definition(native, 'struct NativeRenderFrame {') + ';')
    source = replace_once(source, '// ACTUAL_NATIVE_RESOURCES', definition(native, 'struct NativeResources {') + ';')
    source = replace_once(source, '// ACTUAL_NATIVE_RESOURCE_ACCESS',
                          definition(native, 'NativeResources& resources(void* handle)'))
    bridge = (HERE / 'render_session_rust.inc').read_text()
    for marker, leaf in [('SERVICES', 'render_services'), ('BIND', 'render_bind_page'), ('FINISH', 'render_finish_page')]:
        bridge = replace_once(bridge, '/* ACTUAL_' + marker + '_LEAF */', native_leaf(native, leaf))
    return replace_once(source, '#include "render_session_rust.inc"', bridge)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode', choices=GOLDENS, required=True)
    args = parser.parse_args()
    name = 'reader_render_oracle_' + args.mode
    expected_lines, gzip_sha, raw_sha = GOLDENS[args.mode]
    golden = HERE / ('render-' + args.mode + '.golden.gz')
    compressed = golden.read_bytes()
    expected = gzip.decompress(compressed)
    assert digest(compressed) == gzip_sha and digest(expected) == raw_sha
    counts = collections.Counter(line.split(b' ', 1)[0].decode() for line in expected.splitlines())
    assert len(expected.splitlines()) == expected_lines and counts['case'] == counts['end'] == 5120
    production_file = ROOT / 'rust' / PRODUCTION
    production = production_file.read_text()
    native = (ROOT / NATIVE).read_text()
    fixture_hashes = {str(path.relative_to(ROOT)): digest(path.read_bytes()) for path in (
        HERE / 'render_oracle.cpp', HERE / 'render_session_fixture.h', HERE / 'render_session_rust.inc',
        Path(__file__), HERE / 'test_popup_resource.py',
        ROOT / 'rust/crates/xp-ffi/src/app/epub/test_support/render.rs',
        ROOT / 'rust/crates/xp-ffi/src/app/epub/page.rs',
        ROOT / 'rust/crates/xp-ffi/src/app/epub/render_scope.rs')}
    try:
        with tempfile.TemporaryDirectory(prefix='reader-render-session-' + args.mode + '-') as directory:
            work = Path(directory)
            (work / 'RustEpubResources.cpp').write_text(native)
            (work / 'render.rs').write_text(production)
            baseline_flags = rust_link_flags()

            def compile_run(case, native_source=native, archive=None, lifetime=False):
                source = work / (case + '.cpp')
                source.write_text(fixture(native_source))
                executable = work / case
                flags = baseline_flags if archive is None else rust_link_flags(archive=str(archive))
                command = ['c++', '-std=c++20', '-UNDEBUG', '-O0', '-DRENDER_SESSION_OWNER=1',
                           '-DXP_STRIP_RENDER=' + ('1' if args.mode == 'on' else '0'),
                           '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                           '-I', str(HERE), str(source), *flags, '-o', str(executable)]
                built = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                (work / (case + '.compile.log')).write_bytes(built.stdout + built.stderr)
                assert built.returncode == 0, (case, 'compile failure is not mutant rejection', built.stderr.decode(errors='replace'))
                command = [str(executable)] + (['--lifetime'] if lifetime else [])
                result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                (work / (case + '.run.log')).write_bytes(result.stdout + result.stderr)
                return result

            baseline = compile_run('baseline')
            assert baseline.returncode == 0, baseline.stderr.decode(errors='replace')
            if baseline.stdout != expected:
                for index, (actual, original) in enumerate(zip(baseline.stdout.splitlines(), expected.splitlines()), 1):
                    assert actual == original, (index, original, actual)
                raise AssertionError(('trace length differs', len(expected), len(baseline.stdout)))
            lifetime = compile_run('lifetime', lifetime=True)
            assert lifetime.returncode == 0, lifetime.stderr.decode(errors='replace')
            print(json.dumps({
                'test_name': name, 'cases': 5120, 'trace_lines': expected_lines, 'trace_bytes': len(expected),
                'original_gzip_sha256': gzip_sha, 'original_trace_sha256': raw_sha,
                'actual_trace_sha256': digest(baseline.stdout), 'event_counts': counts,
                'native_source_sha256': digest(native.encode()), 'candidate_source_sha256': digest(production.encode()),
                'fixture_sha256': fixture_hashes, 'lifetime_output_sha256': digest(lifetime.stdout),
                'policy_replacements': ['historical renderStatusBar', 'historical drawClippingHighlights'],
                'scope': 'render_contents under render_with and silent caller-held RenderLock; no full renderBook or App entry-gate claim',
                'lifetime': 'native finish precedes unique Page close; persistent strips drop after historical end line',
                'extra_facts': ['Frequency, FreeHeap, MaxBlock and StoreBw work before Page binding'] +
                               (['actual bound strip painter callback during source detach'] if args.mode == 'on' else []),
            }), flush=True)

            # This is a mutation of the actual native resource class, not of a test expectation.
            root = definition(native, 'struct NativeResources {')
            wrong_root = replace_once(root,
                '      case XP_UI_READER_RENDER_FREQUENCY:\n        return SETTINGS.getRefreshFrequency();',
                '      case XP_UI_READER_RENDER_FREQUENCY:\n        return 0;')
            wrong_native = replace_once(native, root, wrong_root)
            (work / 'RustEpubResources-unbound-frequency.cpp').write_text(wrong_native)
            wrong = compile_run('unbound-frequency', wrong_native, lifetime=True)
            assert wrong.returncode != 0 and b'XP_UI_READER_RENDER_FREQUENCY' in wrong.stderr
            print(json.dumps({'test_name': name, 'mutant': 'unbound-frequency', 'kind': 'production-native',
                              'replacement_count': 1, 'original_source_sha256': digest(native.encode()),
                              'mutant_source_sha256': digest(wrong_native.encode()), 'returncode': wrong.returncode,
                              'stderr_sha256': digest(wrong.stderr)}), flush=True)

            wrong_root = replace_once(root,
                '      case XP_UI_READER_RENDER_FREE_HEAP:\n        return ESP.getFreeHeap();',
                '      case XP_UI_READER_RENDER_FREE_HEAP:\n        return 0;')
            wrong_native = replace_once(native, root, wrong_root)
            wrong = compile_run('unbound-free-heap', wrong_native, lifetime=True)
            assert wrong.returncode != 0 and b'XP_UI_READER_RENDER_FREE_HEAP' in wrong.stderr
            print(json.dumps({'test_name': name, 'mutant': 'unbound-free-heap', 'kind': 'production-native',
                              'replacement_count': 1, 'original_source_sha256': digest(native.encode()),
                              'mutant_source_sha256': digest(wrong_native.encode()), 'returncode': wrong.returncode,
                              'stderr_sha256': digest(wrong.stderr)}), flush=True)

            wrong_root = replace_once(root,
                '      case XP_UI_READER_RENDER_MAX_BLOCK:\n        return ESP.getMaxAllocHeap();',
                '      case XP_UI_READER_RENDER_MAX_BLOCK:\n        return 0;')
            wrong_native = replace_once(native, root, wrong_root)
            wrong = compile_run('unbound-max-block', wrong_native, lifetime=True)
            assert wrong.returncode != 0 and b'XP_UI_READER_RENDER_MAX_BLOCK' in wrong.stderr
            print(json.dumps({'test_name': name, 'mutant': 'unbound-max-block', 'kind': 'production-native',
                              'replacement_count': 1, 'original_source_sha256': digest(native.encode()),
                              'mutant_source_sha256': digest(wrong_native.encode()), 'returncode': wrong.returncode,
                              'stderr_sha256': digest(wrong.stderr)}), flush=True)

            wrong_root = replace_once(root,
                '      case XP_UI_READER_RENDER_STORE_BW:\n        return renderer.storeBwBuffer();',
                '      case XP_UI_READER_RENDER_STORE_BW:\n        return 0;')
            wrong_native = replace_once(native, root, wrong_root)
            wrong = compile_run('unbound-store', wrong_native, lifetime=True)
            assert wrong.returncode != 0 and b'services->execute' in wrong.stderr
            print(json.dumps({'test_name': name, 'mutant': 'unbound-store', 'kind': 'production-native',
                              'replacement_count': 1, 'original_source_sha256': digest(native.encode()),
                              'mutant_source_sha256': digest(wrong_native.encode()), 'returncode': wrong.returncode,
                              'stderr_sha256': digest(wrong.stderr)}), flush=True)

            copied_repo = work / 'source'
            copied_rust = copied_repo / 'rust'
            shutil.copytree(ROOT / 'rust', copied_rust, ignore=shutil.ignore_patterns('target', '__pycache__'))
            for item in ('scripts', 'lib', 'src'):
                (copied_repo / item).symlink_to(ROOT / item, target_is_directory=True)
            copied_source = copied_rust / PRODUCTION
            assert copied_source.read_text() == production
            target = work / 'target'
            for mutant, before, after, check_lifetime in RUST_MUTANTS:
                changed = replace_once(production, before, after)
                copied_source.write_text(changed)
                subprocess.run(['cargo', 'build', '--manifest-path', str(copied_rust / 'Cargo.toml'),
                                '--target-dir', str(target), '-p', 'xp-ffi', '--release', '--locked',
                                '--features', 'app-test'], cwd=copied_rust, check=True)
                archive = target / 'release/libxp_ffi.a'
                result = compile_run(mutant, archive=archive)
                assert result.returncode != 0 or result.stdout != expected, (mutant, 'actual Rust mutant survived')
                evidence = {'test_name': name, 'mutant': mutant, 'kind': 'production-rust', 'replacement_count': 1,
                            'original_source_sha256': digest(production.encode()), 'mutant_source_sha256': digest(changed.encode()),
                            'returncode': result.returncode, 'trace_sha256': digest(result.stdout),
                            'stderr_sha256': digest(result.stderr)}
                if check_lifetime:
                    rejected = compile_run(mutant + '-lifetime', archive=archive, lifetime=True)
                    assert rejected.returncode != 0 and b'native frame must finish before Page closes' in rejected.stderr
                    evidence['lifetime_returncode'] = rejected.returncode
                    evidence['lifetime_stderr_sha256'] = digest(rejected.stderr)
                print(json.dumps(evidence), flush=True)
    finally:
        assert golden.read_bytes() == compressed, 'immutable original golden changed'
        assert production_file.read_text() == production, 'real production Rust source changed'
        assert (ROOT / NATIVE).read_text() == native, 'real production native source changed'
    print(f'{name}: 5120 exact cases; lifetime facts; five Rust and three native mutants rejected')


if __name__ == '__main__':
    main()
