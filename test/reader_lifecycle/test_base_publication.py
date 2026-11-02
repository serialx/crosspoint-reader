"""Run the production base publication helpers while a paint is blocked."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile
ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'test'))
from rust_host import rust_link_flags
sys.path.insert(0, str(HERE))
from extract import definition
from test_lifecycle_boundary import retirement_absence
retirement_absence(ROOT)
source = (ROOT / 'src/activities/reader/ReaderActivity.cpp').read_text()
methods = ['void ReaderActivity::render(', 'uint32_t ReaderActivity::readBaseField(',
           'void ReaderActivity::writeBaseField(', 'ReaderSession ReaderActivity::snapshotReaderSession(',
           'void ReaderActivity::resetReaderSession(', 'void ReaderActivity::completeReaderSession(',
           'void ReaderActivity::queueForcedRefresh(', 'void ReaderActivity::setRefreshCounter(',
           'int32_t& ReaderActivity::renderRefreshCounter(', 'bool ReaderActivity::takeForcedRefresh(',
           'void ReaderActivity::markPageRendered(', 'bool ReaderActivity::acceptsRemoteInput(',
           'void ReaderActivity::notePageTurn(']
fixture = (HERE / 'base_publication.cpp').read_text().replace('// BASE_PUBLICATION_METHODS', '\n'.join(definition(source, m) for m in methods))
with tempfile.TemporaryDirectory() as directory:
    p = Path(directory)
    cpp = p / 'base.cpp'
    cpp.write_text(fixture)
    subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer', str(cpp), *rust_link_flags(), '-o', str(p / 'base')], check=True)
    subprocess.run([str(p / 'base')], check=True)
    nested = subprocess.run([str(p / 'base'), 'nested'], capture_output=True, text=True)
    assert nested.returncode != 0 and 'renderFrame' in nested.stderr
    (p / 'abi.c').write_text('''#include <xp_ui_reader.h>
void (*begin)(XpUiReaderBaseModel*, XpUiReaderBaseModel*) = xp_ui_reader_base_begin_render;
void (*publish)(XpUiReaderBaseModel*, const XpUiReaderBaseModel*) = xp_ui_reader_base_publish;
void (*force)(XpUiReaderBaseModel*) = xp_ui_reader_base_force_refresh;
''')
    subprocess.run(['cc', '-std=c11', '-Werror', '-fsyntax-only', str(p / 'abi.c'), '-I' + str(ROOT / 'rust/crates/xp-ffi/include')], check=True)
    header = (ROOT / 'rust/crates/xp-ffi/include/xp_ui_reader.h').read_text()
    for module, family, kinds in [('base', 'BASE', ['Field', 'Query', 'Command', 'Operation'])]:
        rust = (ROOT / f'rust/crates/xp-ui/src/reader/{module}.rs').read_text()
        for kind in kinds:
            body = re.search(r'pub enum ' + kind + r' \{(.*?)\n\}', rust, re.S)[1]
            for name, value in re.findall(r'(\w+) = (\d+)', body):
                suffix = re.sub(r'(?<!^)(?=[A-Z])', '_', name).upper()
                key = 'XP_UI_READER_' + family + '_' + kind.upper() + '_' + suffix
                assert re.search(r'\b' + key + r' = ' + value + r'\b', header), key
    drift = fixture.replace('xp_ui_reader_base_publish', 'xp_ui_reader_base_publish_drift')
    cpp.write_text(drift)
    failed = subprocess.run(['c++', '-std=c++17', '-fsyntax-only', str(cpp), '-I' + str(ROOT / 'rust/crates/xp-ffi/include')], capture_output=True, text=True)
    assert failed.returncode != 0 and 'xp_ui_reader_base_publish_drift' in failed.stderr
    print('base publication: actual input helpers finish during busy paint; pending turn/manual/counter survive once; shared C ABI, base enum drift, retired-name absence, nested render and ASan/UBSan checks pass')

# Append owner coverage; all shared/XTC executables and ABI/drift checks above stay intact.
import hashlib
import json
import shutil
import signal


def owner_mutation(source, before, after):
    if source.count(before) != 1:
        raise ValueError(f'Expected one owner mutant anchor: {before}')
    return source.replace(before, after, 1)


def run_owner_publication():
    owner_source = HERE / 'base_session_publication.cpp'
    scope_path = Path('crates/xp-ffi/src/app/epub/render_scope.rs')
    state_path = Path('crates/xp-ffi/src/app/epub/state.rs')
    originals = {path: (ROOT / 'rust' / path).read_text() for path in [scope_path, state_path]}
    with tempfile.TemporaryDirectory(prefix='reader-owner-publication-') as directory:
        work = Path(directory)
        baseline_flags = rust_link_flags()

        def executable(name, archive=None):
            binary = work / name
            flags = baseline_flags if archive is None else rust_link_flags(archive=str(archive))
            subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-pthread',
                            '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                            '-fno-omit-frame-pointer', '-I', str(HERE), str(owner_source),
                            *flags, '-o', str(binary)], check=True)
            return binary

        baseline = executable('owner-publication')
        subprocess.run([str(baseline)], check=True)
        nested = subprocess.run([str(baseline), 'nested'], capture_output=True, text=True)
        # The no_std Rust panic handler aborts without printing the panic text.
        # Reaching the inner callback exits 77; returning from nested render exits 78.
        assert nested.returncode == -signal.SIGABRT and 'owner-nested-entry' in nested.stderr
        assert 'unexpected-nested-paint' not in nested.stderr

        copied_repo = work / 'source'
        copied_rust = copied_repo / 'rust'
        shutil.copytree(ROOT / 'rust', copied_rust,
                        ignore=shutil.ignore_patterns('target', '__pycache__'))
        for name in ('scripts', 'lib', 'src'):
            (copied_repo / name).symlink_to(ROOT / name, target_is_directory=True)
        target = work / 'target'
        mutants = [
            ('pending-input-overwritten', scope_path,
             'state.base.session.pending_forward = pending;',
             'state.base.session.pending_forward = false; let _ = pending;',
             'pending input must survive publication'),
            ('counter-only-lost', scope_path,
             'if state.base.refresh_requests == 0 {',
             'if state.base.refresh_requests & REFRESH_MANUAL == 0 {',
             'counter-only refresh must survive publication'),
            ('elapsed-two-seconds-lost', state_path,
             'if elapsed >= 2 {', 'if elapsed > 2 {', 'session duration'),
        ]
        for name, path, before, after, expected_assertion in mutants:
            for original_path, original in originals.items():
                (copied_rust / original_path).write_text(original)
            changed = owner_mutation(originals[path], before, after)
            (copied_rust / path).write_text(changed)
            print(json.dumps({'mutant': name, 'path': str(path), 'replacement_count': 1,
                              'original_sha256': hashlib.sha256(originals[path].encode()).hexdigest(),
                              'mutant_sha256': hashlib.sha256(changed.encode()).hexdigest()}), flush=True)
            subprocess.run(['cargo', 'build', '--manifest-path', str(copied_rust / 'Cargo.toml'),
                            '--target-dir', str(target), '-p', 'xp-ffi', '--release', '--locked',
                            '--features', 'app-test'], cwd=copied_rust, check=True)
            binary = executable(name, target / 'release/libxp_ffi.a')
            failed = subprocess.run([str(binary)], capture_output=True, text=True)
            assert failed.returncode != 0 and expected_assertion in failed.stderr, (
                name, failed.returncode, failed.stdout, failed.stderr)
        for path, original in originals.items():
            assert (ROOT / 'rust' / path).read_text() == original
    print('owner publication: blocked-paint input, pending/manual/counter/disable-fast once-only '
          'publication, remembered bit, physical progress completion, accounting boundaries, '
          'nested rejection and three Rust mutants pass; ready false/true is a render scope gate '
          'only, not full app entry/failed-entry integration')


run_owner_publication()
