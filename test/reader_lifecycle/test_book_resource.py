"""Compose Rust book resource owners with actual native leaves and physical fakes."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

from test_popup_resource import definition, native_leaf, replace_once

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'test'))
from rust_host import rust_link_flags

LEAVES = ('file_open_read', 'file_read', 'file_close', 'file_seek', 'file_size',
          'path_remove_tree', 'protection_open', 'protection_close',
          'protection_encrypted', 'protection_size', 'protection_stream',
          'cache_lock', 'cache_unlock')


def main():
    native = (ROOT / 'src/activities/reader/RustEpubResources.cpp').read_text()
    package = (ROOT / 'lib/Epub/Epub/PackagePlatform.cpp').read_text()
    template = (HERE / 'book_resource_oracle.cpp').read_text()
    text = '\n'.join(definition(native, signature) for signature in (
        'XpAppText text(const std::string& value)',
        'const char* pathText(XpAppText value)', 'std::string owned(XpAppText value)'))
    mutex = '\n'.join(definition(package, signature) for signature in (
        'void BookMetadataCache::lockReadCursor()', 'void BookMetadataCache::unlockReadCursor()'))
    source = replace_once(template, '// ACTUAL_TEXT_HELPERS', text)
    source = replace_once(source, '// ACTUAL_MUTEX_METHODS', mutex)
    leaves = '\n'.join('  value.' + name + ' = ' + native_leaf(native, name) + ';' for name in LEAVES)
    source = replace_once(source, '// ACTUAL_LEAVES', leaves)
    configured = os.environ.get('XP_BOOK_RESOURCE_EVIDENCE_DIR')
    output = Path(configured) if configured else Path(tempfile.mkdtemp(prefix='book-resource-'))
    output.mkdir(parents=True, exist_ok=True)
    archive = rust_link_flags()
    rows = []
    cases = [('production', source, None),
             ('missing-unlock', replace_once(source,
              'value.cache_unlock = ' + native_leaf(native, 'cache_unlock') + ';',
              'value.cache_unlock = [] {};'), 'sink must run after cache guard release'),
             ('missing-protection-close', replace_once(source,
              'value.protection_close = ' + native_leaf(native, 'protection_close') + ';',
              'value.protection_close = [](void*) {};'), 'born == died')]
    for name, content, expected in cases:
        directory = output / name
        directory.mkdir(exist_ok=False)
        cpp = directory / 'oracle.cpp'
        cpp.write_text(content)
        binary = directory / 'oracle'
        command = [os.environ.get('CXX', 'c++'), '-std=c++20', '-O0', '-g', '-UNDEBUG',
                   '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                   '-DXP_FFI_HOST_FAULT_INJECTION', '-I', str(ROOT / 'lib/Memory'),
                   '-I', str(HERE), '-I', str(ROOT / 'test/ui_runtime'),
                   '-I', str(ROOT / 'freeink-sdk/libs/book/ContentProtection/include'),
                   str(cpp), *archive, '-o', str(binary)]
        built = subprocess.run(command, capture_output=True, text=True)
        (directory / 'compile.log').write_text(built.stdout + built.stderr)
        if built.returncode:
            raise RuntimeError(f'{name} did not compile:\n{built.stderr}')
        ran = subprocess.run([str(binary)], capture_output=True, text=True)
        log = ran.stdout + ran.stderr
        (directory / 'run.log').write_text(log)
        if expected is None:
            if ran.returncode:
                raise RuntimeError(f'Production composition failed:\n{log}')
        elif ran.returncode == 0 or expected not in log:
            raise RuntimeError(f'Mutant did not reach {expected}:\n{log}')
        rows.append(dict(name=name,command=command,compile_returncode=built.returncode,
                         run_returncode=ran.returncode,expected_assertion=expected,
                         source_sha256=hashlib.sha256(content.encode()).hexdigest()))
    (output / 'results.json').write_text(json.dumps(dict(tests=rows,
        native_sha256=hashlib.sha256(native.encode()).hexdigest(),
        package_sha256=hashlib.sha256(package.encode()).hexdigest(),
        archive_sha256=hashlib.sha256(Path(archive[-1]).read_bytes()).hexdigest()), indent=2) + '\n')
    print('Book resource composition: real Rust RAII and native leaves; file/protection errors, '
          'non-UTF-8 bytes, guard release before sink, allocation-failure cleanup; two mutants rejected.')


if __name__ == '__main__':
    main()
