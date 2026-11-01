"""Compare real owner More actions with extracted native menu construction."""
import hashlib
import json
from pathlib import Path
import re
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
NATIVE = Path('src/activities/reader/EpubReaderMenuActivity.cpp')
HEADER = Path('src/activities/reader/EpubReaderMenuActivity.h')


def digest(data):
    return hashlib.sha256(data).hexdigest()


def replace_once(source, before, after):
    assert source.count(before) == 1, ('expected one replacement', before)
    return source.replace(before, after, 1)


def main():
    native = (ROOT / NATIVE).read_bytes()
    header = (ROOT / HEADER).read_bytes()
    method = definition(native.decode(), 'void EpubReaderMenuActivity::buildMenuItems(')
    action_enum = definition(header.decode(), 'enum class MenuAction') + ';'
    capacity = re.findall(r'static constexpr size_t MAX_MENU_ITEMS = [0-9]+;', header.decode())
    assert len(capacity) == 1
    # Labels are opaque to this action-order oracle; the native method chooses all items.
    label_names = sorted(set(re.findall(r'StrId::(\w+)', method)))
    assert label_names
    label_enum = 'enum class StrId { ' + ', '.join(label_names) + ' };'
    fixture_path = HERE / 'more_actions_session.cpp'
    fixture = fixture_path.read_text()
    for marker, value in [('NATIVE_LABEL_ENUM', label_enum), ('NATIVE_MENU_ENUM', action_enum),
                          ('NATIVE_MENU_CAPACITY', capacity[0]), ('NATIVE_MENU_METHOD', method)]:
        fixture = replace_once(fixture, '// ' + marker, value)
    production_file = ROOT / 'rust' / PRODUCTION
    production = production_file.read_bytes()
    before = b'        if footnotes {\n            add(Action::Footnotes);\n        }'
    after = b'        if true {\n            add(Action::Footnotes);\n        }'
    changed = replace_once(production, before, after)
    fixture_hashes = {path.name: digest(path.read_bytes()) for path in (
        fixture_path, Path(__file__), HERE / 'overlay_actions_session_fixture.h',
        ROOT / 'rust/crates/xp-ffi/src/app/epub/test_support/overlay_actions.rs')}
    try:
        with tempfile.TemporaryDirectory(prefix='reader-more-actions-') as directory:
            work = Path(directory)
            flags = rust_link_flags()

            def run(name, archive=None):
                path = work / (name + '.cpp')
                path.write_text(fixture)
                binary = work / name
                link_flags = flags if archive is None else rust_link_flags(archive=str(archive))
                subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-O0', '-fsanitize=address,undefined',
                                '-I', str(HERE), str(path), *link_flags, '-o', str(binary)], check=True)
                return subprocess.run([str(binary)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)

            baseline = run('more-actions')
            assert baseline.returncode == 0, baseline.stderr.decode(errors='replace')
            assert baseline.stdout == (b'more actions: 128 native-order cases; two publication-gate '
                                       b'interleavings; lazy input facts; recovery\n')
            copied_repo = work / 'source'
            copied_rust = copied_repo / 'rust'
            shutil.copytree(ROOT / 'rust', copied_rust, ignore=shutil.ignore_patterns('target', '__pycache__'))
            for name in ('scripts', 'lib', 'src'):
                (copied_repo / name).symlink_to(ROOT / name, target_is_directory=True)
            copied_source = copied_rust / PRODUCTION
            assert copied_source.read_bytes() == production
            copied_source.write_bytes(changed)
            target = work / 'target'
            # Compilation must succeed; a compile failure never counts as a killed mutant.
            subprocess.run(['cargo', 'build', '--manifest-path', str(copied_rust / 'Cargo.toml'),
                            '--target-dir', str(target), '-p', 'xp-ffi', '--release', '--locked',
                            '--features', 'app-test'], cwd=copied_rust, check=True)
            mutant = run('footnotes-unconditional', target / 'release/libxp_ffi.a')
            assert mutant.returncode != 0, 'actual Rust footnotes mutant survived'
            stderr = mutant.stderr.decode(errors='replace')
            assert 'assertActions' in stderr and 'actual.count == expected.size()' in stderr, stderr
            print(json.dumps({
                'test_name': 'reader_more_actions_session', 'flag_combinations': 128,
                'publication_gate_cases': 2, 'recovery_cases': 1,
                'scope': 'real build_more_actions without policy hooks; native buildMenuItems excluding Chapter/Text',
                'resource_contract': 'footnotes under first lock; ordinary physical probes outside ModelLock; '
                    'second acquisition gates publication; input_facts only when frontlight is present',
                'fixture_sha256': fixture_hashes, 'native_source_sha256': digest(native),
                'native_header_sha256': digest(header), 'native_method_sha256': digest(method.encode()),
                'native_enum_sha256': digest(action_enum.encode()), 'candidate_source_sha256': digest(production),
                'baseline_returncode': baseline.returncode, 'baseline_stdout_sha256': digest(baseline.stdout),
                'mutant': 'footnotes-unconditional', 'kind': 'production-rust', 'source': str(PRODUCTION),
                'replacement_count': 1, 'mutant_source_sha256': digest(changed),
                'mutant_returncode': mutant.returncode, 'mutant_stderr_sha256': digest(mutant.stderr),
            }), flush=True)
    finally:
        assert production_file.read_bytes() == production, 'real production source changed'
        assert (ROOT / NATIVE).read_bytes() == native and (ROOT / HEADER).read_bytes() == header
    print('more actions: actual native order and both gate interleavings passed; actual Rust mutant rejected')


if __name__ == '__main__':
    main()
