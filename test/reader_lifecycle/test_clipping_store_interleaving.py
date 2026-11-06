"""Verify the Rust store's physical prepare/I/O/publish interleaving.

The historical child-parent trace observes logical transactions. This independent
composition installs no logical observer and checks the actual mutex and I/O order.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

PRODUCTION = Path('crates/xp-ffi/src/app/epub/clipping.rs')
LIMITATIONS = [
    'Host mutex ordering and physical in-memory filesystem, not target timing or FAT power-loss evidence.',
    'Real reader insert/remove, NativeStore, service lease and teardown; no logical transaction observer.',
    'Clean teardown after recovery is covered; dirty unload persistence is covered by the byte oracle.',
]


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, default=Path.cwd())
    parser.add_argument('--evidence', type=Path)
    args = parser.parse_args()
    root = args.repo.resolve()
    sys.path.insert(0, str(root / 'test'))
    from rust_host import rust_link_flags
    production = (root / 'rust' / PRODUCTION).read_text()
    # Preserve exact mutation sites while allowing rustfmt to change indentation.
    prepare = re.search(
        r'(?m)^([ \t]+)let _access = self\.lock_resources\(\);\n'
        r'\1operation\.update\(\|store\| store\.prepare_add\(record, chapter, text\)\)', production)
    publish = re.search(
        r'(?m)^([ \t]+)let _access = self\.lock_resources\(\);\n'
        r'\1operation\.update\(\|store\| store\.publish_clean\(written\)\);\n'
        r'\1AddResult::Added', production)
    persist = re.search(
        r'(?m)^([ \t]+)let Ok\(written\) = operation\.read\(\|store\| store\.persist\(pending, Some\(text\)\)\) else \{', production)
    assert prepare and publish and persist
    mutations = {
        'prepare-guard-omitted': (prepare[0], prepare[0].split('\n', 1)[1]),
        'publish-guard-omitted': (publish[0], publish[0].split('\n', 1)[1]),
        'persistence-under-render-lock': (
            persist[0], persist[1] + 'let _long_guard = self.lock_resources();\n' + persist[0]),
    }
    for before, _ in mutations.values():
        assert production.count(before) == 1
    paths = list((root / 'rust/crates/xp-ffi/src/app/clippings').glob('*.rs'))
    paths += [root / 'rust' / PRODUCTION, Path(__file__).resolve()]
    paths += [root / name for name in (
        'rust/crates/xp-ffi/src/app/epub/test_support/clipping_store.rs',
        'rust/crates/xp-ffi/src/app/epub/test_support/clipping.rs',
        'rust/crates/xp-ffi/src/app/epub/lifetime.rs',
        'test/clipping_store/ClippingInterleaving.cpp', 'test/clipping_store/reader_fixture.h',
        'test/clipping_store/reader_storage.h', 'test/owner_tables.h',
        'test/reader_lifecycle/clipping_owner_fixture.h',
        'test/reader_lifecycle/legacy_book_fixture.h', 'test/library_builder/stubs/HalStorage.h',
        'test/rust_host.py', 'test/cmake/xp_ffi_host.cpp')]
    inputs = {str(path.relative_to(root)): digest(path.read_bytes()) for path in paths}
    evidence = args.evidence.resolve() if args.evidence else None
    if evidence:
        evidence.mkdir(parents=True, exist_ok=False)
    commands = []
    report = dict(test_name='reader_clipping_store_interleaving', status='started',
                  input_sha256=inputs, limitations=LIMITATIONS, mutants=[])

    def save(name, data):
        if evidence:
            (evidence / name).write_bytes(data)

    def command(argv, label, cwd=None, required=True):
        commands.append(dict(label=label, argv=list(map(str, argv)), cwd=str(cwd or root)))
        result = subprocess.run(list(map(str, argv)), cwd=cwd or root, capture_output=True, timeout=600)
        save(label + '.stdout', result.stdout)
        save(label + '.stderr', result.stderr)
        if required:
            assert result.returncode == 0, (label, result.returncode, result.stderr.decode(errors='replace'))
        return result

    try:
        with tempfile.TemporaryDirectory(prefix='reader-clipping-store-interleaving-') as directory:
            work = Path(directory)
            flags = rust_link_flags()
            report['baseline_archive_sha256'] = digest(Path(flags[-1]).read_bytes())
            def run(name, archive=None):
                binary = work / name
                link = flags if archive is None else rust_link_flags(archive=str(archive))
                command(['c++', '-std=c++20', '-UNDEBUG', '-O0', '-pthread',
                         '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-DXP_STRIP_RENDER=0',
                         '-I', root / 'test', '-I', root / 'test/reader_lifecycle',
                         root / 'test/clipping_store/ClippingInterleaving.cpp', *link, '-o', binary], name + '-compile')
                return command([binary], name + '-run', required=False)
            baseline = run('baseline')
            assert baseline.returncode == 0, baseline.stderr.decode(errors='replace')
            assert baseline.stdout == b'clipping physical sequence: four exact success/failure routes, old metadata during I/O, atomic publication and teardown\n'
            report['baseline_stdout_sha256'] = digest(baseline.stdout)
            copied = work / 'source'
            shutil.copytree(root / 'rust', copied / 'rust', ignore=shutil.ignore_patterns('target', '__pycache__'))
            for name in ('scripts', 'lib', 'src'):
                (copied / name).symlink_to(root / name, target_is_directory=True)
            target = work / 'target'
            for name, (before, after) in mutations.items():
                changed = production.replace(before, after, 1)
                (copied / 'rust' / PRODUCTION).write_text(changed)
                save(name + '.rs', changed.encode())
                command(['cargo', 'build', '--offline', '--manifest-path', copied / 'rust/Cargo.toml',
                         '--target-dir', target, '-p', 'xp-ffi', '--release', '--locked', '--features', 'app-test'],
                        name + '-cargo', cwd=copied / 'rust')
                result = run(name, target / 'release/libxp_ffi.a')
                assert result.returncode != 0 and b'Assertion failed' in result.stderr, (name, result.returncode, result.stderr)
                marker = b'!renderDepth && !modelDepth' if name == 'persistence-under-render-lock' else b'order == expected'
                assert marker in result.stderr, result.stderr
                assert b'AddressSanitizer' not in result.stderr and b'runtime error:' not in result.stderr
                report['mutants'].append(dict(name=name, source_sha256=digest(changed.encode()),
                                             returncode=result.returncode, stderr_sha256=digest(result.stderr)))
            report['status'] = 'passed'
    except BaseException as error:
        report.update(status='failed', error=repr(error))
        raise
    finally:
        report['commands'] = commands
        report['inputs_unchanged'] = all(digest(path.read_bytes()) == inputs[str(path.relative_to(root))] for path in paths)
        save('manifest.json', (json.dumps(report, indent=2) + '\n').encode())
        assert report['inputs_unchanged'], 'production or input fixture changed while harness ran'
    print(json.dumps(report), flush=True)


if __name__ == '__main__':
    main()
