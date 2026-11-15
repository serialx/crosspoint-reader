"""Run typed Rust package/ZIP/cache owners against frozen cache-file expectations.

The existing PackageGoldens test still compares the complete native call trace.
Here ZIP is physical. Three cases are excluded by coordinator ruling: the old
oracle collapses NFD/NFC names and accepts names beyond the physical ZIP limit.
They require separate complete native/Rust load comparisons.
"""
import gzip
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile
from test_popup_resource import definition, native_leaf, replace_once
ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'test'))
from rust_host import rust_link_flags
spec = importlib.util.spec_from_file_location('package_frozen', ROOT / 'test/package_golden/run.py')
frozen = importlib.util.module_from_spec(spec)
spec.loader.exec_module(frozen)


def diagnostics(output, native):
    source = (HERE / 'book_package_diagnostics.cpp').read_text()
    package = (ROOT / 'lib/Epub/Epub/PackagePlatform.cpp').read_text()
    events = (ROOT / 'lib/Epub/Epub/parsers/PackageDiagnostics.h').read_text()
    source = replace_once(source, '// ACTUAL_PACKAGE_DIAGNOSTIC', definition(events, 'inline void packageDiagnostic('))
    source = replace_once(source, '// ACTUAL_PACKAGE_SETUP', definition(package, 'bool PackageParser::setup()'))
    source = replace_once(source, '// ACTUAL_PACKAGE_ERROR', definition(package, 'void PackageParser::logError('))
    source = replace_once(source, '// ACTUAL_LOG_LEAVES', '\n'.join('  value.' + name + ' = ' + native_leaf(native, name) + ';' for name in ('log', 'log_enabled')))
    cpp = output / 'diagnostics.cpp'; cpp.write_text(source)
    binary = output / 'diagnostics'
    command = [os.environ.get('CXX', 'c++'), '-std=c++20', '-O0', '-g', '-UNDEBUG',
               '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-DXP_FFI_HOST_FAULT_INJECTION',
               '-I', str(HERE), '-I', str(ROOT / 'test/ui_runtime'), '-I', str(ROOT / 'lib/expat'),
               str(cpp), str(ROOT / 'lib/expat/ExpatErrors.cpp'), *rust_link_flags(), '-o', str(binary)]
    built = subprocess.run(command, capture_output=True, timeout=120)
    (output / 'diagnostics-compile.log').write_bytes(built.stdout + built.stderr)
    if built.returncode: raise RuntimeError(built.stderr.decode())
    ran = subprocess.run([str(binary)], capture_output=True, timeout=120)
    (output / 'diagnostics-run.log').write_bytes(ran.stdout + ran.stderr)
    if ran.returncode: raise RuntimeError(ran.stderr.decode())
    return dict(command=command, cases=1895, returncode=ran.returncode,
                generated_source_sha256=hashlib.sha256(source.encode()).hexdigest())


def main():
    native = (ROOT / 'src/activities/reader/RustEpubResources.cpp').read_text()
    app = (ROOT / 'src/activities/RustAppResources.cpp').read_text()
    source = (HERE / 'book_package_oracle.cpp').read_text()
    helpers = '\n'.join(definition(native, signature) for signature in (
        'XpAppText text(const std::string& value)', 'const char* pathText(XpAppText value)', 'std::string owned(XpAppText value)'))
    source = replace_once(source, '// ACTUAL_TEXT_HELPERS', helpers)
    source = replace_once(source, '// ACTUAL_CSTR_HELPER', definition(app, 'const char* cstr(XpAppText value)'))
    fields = ('file_open_read', 'file_open_write', 'file_read', 'file_write', 'file_close', 'file_seek', 'file_size', 'path_remove', 'cache_lock', 'cache_unlock', 'scratch_claim', 'scratch_release')
    source = replace_once(source, '// ACTUAL_LEAVES', '\n'.join('  value.' + name + ' = ' + native_leaf(native, name) + ';' for name in fields))
    source = replace_once(source, '// ACTUAL_ALLOCATOR_LEAVES', '\n'.join('  value.' + name + ' = ' + native_leaf(app, name) + ';' for name in ('buffer_allocate', 'buffer_release', 'path_exists')))
    output = Path(os.environ.get('XP_BOOK_PACKAGE_EVIDENCE_DIR') or tempfile.mkdtemp(prefix='book-package-'))
    output.mkdir(parents=True, exist_ok=True)
    diagnostic_result = diagnostics(output, native)
    cpp = output / 'oracle.cpp'; cpp.write_text(source)
    binary = output / 'oracle'
    command = [os.environ.get('CXX', 'c++'), '-std=c++20', '-O0', '-g', '-UNDEBUG',
               '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-DXP_FFI_HOST_FAULT_INJECTION',
               '-I', str(HERE / 'scratch_stubs'), '-I', str(ROOT / 'lib/Memory'), '-I', str(HERE), '-I', str(ROOT / 'test/ui_runtime'),
               '-I', str(ROOT / 'test/chapter_html_slim_parser/stubs'), str(cpp), str(ROOT / 'lib/Memory/BuildScratch.cpp'), *rust_link_flags(), '-o', str(binary)]
    built = subprocess.run(command, capture_output=True, timeout=120)
    (output / 'compile.log').write_bytes(built.stdout + built.stderr)
    if built.returncode: raise RuntimeError(built.stderr.decode())
    cases = []
    for path in sorted((ROOT / 'test/epubs').glob('*.epub')):
        with zipfile.ZipFile(path) as archive:
            cases.append((path.stem, {name: archive.read(name) for name in archive.namelist() if not name.endswith('/')}))
    cases += [('synthetic-' + name, files) for name, files in frozen.synthetic()]
    results, excluded, failures = [], [], []
    for name, files in cases:
        if name == 'synthetic-unicode-nfd' or any(len(path.encode()) >= 256 for path in files):
            reason = 'Frozen host filesystem map collapses NFD/NFC names' if name == 'synthetic-unicode-nfd' else 'Frozen package oracle ZIP map supports names beyond the physical ZIP reader limit'
            excluded.append(dict(name=name, reason=reason))
            continue
        golden_path = ROOT / 'test/package_golden/goldens' / (name + '.json.gz')
        golden = json.loads(gzip.decompress(golden_path.read_bytes()))
        case = output / name; case.mkdir()
        archive_path = case / 'source.epub'
        with zipfile.ZipFile(archive_path, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
            for path, data in files.items(): archive.writestr(path, data)
        schedules = []
        for mode in ('nav', 'ncx'):
            for chunk in frozen.CHUNKS:
                schedule = f'{mode}-{chunk}'
                cache = case / schedule
                ran = subprocess.run([str(binary), str(archive_path), str(cache), str(chunk), mode], capture_output=True, timeout=60)
                (case / (schedule + '.log')).write_bytes(ran.stdout + ran.stderr)
                expected = golden['schedules'][schedule]
                match = ran.returncode == expected['status']
                comparisons = {}
                for filename in ('spine.bin.tmp', 'toc.bin.tmp', 'book.bin'):
                    path = cache / filename
                    actual = path.read_bytes() if path.exists() else None
                    encoded = expected['files'].get(filename)
                    wanted = bytes.fromhex(encoded) if encoded is not None else None
                    comparisons[filename] = dict(exact=actual == wanted,
                        actual_sha256=None if actual is None else hashlib.sha256(actual).hexdigest(),
                        expected_sha256=None if wanted is None else hashlib.sha256(wanted).hexdigest())
                    match &= actual == wanted
                schedules.append(dict(schedule=schedule, returncode=ran.returncode, expected_returncode=expected['status'], files=comparisons))
                if not match: failures.append(f'{name}/{schedule}')
        results.append(dict(name=name, golden_sha256=hashlib.sha256(golden_path.read_bytes()).hexdigest(), schedules=schedules))
    (output / 'results.json').write_text(json.dumps(dict(command=command, diagnostics=diagnostic_result, cases=results, exclusions=excluded, failures=failures), indent=2) + '\n')
    assert not failures, failures[:20]
    print(f'{len(results)} frozen package cases x 12 chunk/mode schedules: exact cache bytes and failure status; {len(excluded)} host-filesystem/ZIP-map cases separately excluded.')


if __name__ == '__main__': main()
