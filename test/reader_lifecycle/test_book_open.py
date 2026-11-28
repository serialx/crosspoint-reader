"""Preserve raw SDK open-failure diagnostics through the Rust book adapters."""
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


def main():
    native = (ROOT / 'src/activities/reader/RustEpubResources.cpp').read_text()
    hal = (ROOT / 'lib/hal/HalStorage.cpp').read_text()
    sdk = (ROOT / 'freeink-sdk/libs/hardware/SDCardManager/src/SDCardManager.cpp').read_text()
    source = (HERE / 'book_open_oracle.cpp').read_text()
    source = replace_once(source, '// ACTUAL_SDK_OPEN', '\n'.join(
        definition(sdk, f'bool SDCardManager::openFileFor{mode}(const char* moduleName, const char* path, FsFile& file)')
        for mode in ('Read', 'Write')))
    source = replace_once(source, '// ACTUAL_HAL_OPEN', '\n'.join(
        definition(hal, f'bool HalStorage::openFileFor{mode}(const char* moduleName, const char* path, HalFile& file)')
        for mode in ('Read', 'Write')))
    source = replace_once(source, '// ACTUAL_TEXT_HELPERS', definition(native, 'const char* pathText(XpAppText value)'))
    source = replace_once(source, '// ACTUAL_OPEN_LEAVES', '\n'.join(
        'value.' + field + ' = ' + native_leaf(native, field) + ';'
        for field in ('file_open_read', 'file_open_write', 'file_close')))
    output = Path(os.environ.get('XP_BOOK_OPEN_EVIDENCE_DIR') or tempfile.mkdtemp(prefix='book-open-'))
    output.mkdir(parents=True, exist_ok=True)
    cpp = output / 'oracle.cpp'
    cpp.write_text(source)
    binary = output / 'oracle'
    command = [os.environ.get('CXX', 'c++'), '-std=c++20', '-O0', '-g', '-UNDEBUG',
               '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-DXP_FFI_HOST_FAULT_INJECTION',
               '-I', str(ROOT / 'lib/Memory'), '-I', str(HERE), '-I', str(ROOT / 'test/ui_runtime'),
               str(cpp), *rust_link_flags(), '-o', str(binary)]
    built = subprocess.run(command, capture_output=True, timeout=120)
    (output / 'compile.log').write_bytes(built.stdout + built.stderr)
    if built.returncode:
        raise RuntimeError(built.stderr.decode())
    ran = subprocess.run([str(binary)], capture_output=True, timeout=120)
    (output / 'run.log').write_bytes(ran.stdout + ran.stderr)
    if ran.returncode:
        raise RuntimeError(ran.stderr.decode())
    (output / 'results.json').write_text(json.dumps(dict(
        command=command, returncode=ran.returncode, cases=28,
        source_sha256=hashlib.sha256(source.encode()).hexdigest(),
        native_sha256=hashlib.sha256(native.encode()).hexdigest(),
        hal_sha256=hashlib.sha256(hal.encode()).hexdigest(),
        sdk_sha256=hashlib.sha256(sdk.encode()).hexdigest(),
        archive_sha256=hashlib.sha256(Path(next(arg for arg in command if arg.endswith('/libxp_ffi.a'))).read_bytes()).hexdigest(),
    ), indent=2) + '\n')
    print('28 open-failure cases pass: original SDK tags/text, serial connection gate, flags, lock lifetime and non-UTF-8 paths.')


if __name__ == '__main__':
    main()
