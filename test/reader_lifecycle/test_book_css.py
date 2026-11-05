"""Compose the owned CSS transaction with actual file/allocator leaves."""
import json
import hashlib
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
    app = (ROOT / 'src/activities/RustAppResources.cpp').read_text()
    source = (HERE / 'book_css_oracle.cpp').read_text()
    helpers = '\n'.join(definition(native, signature) for signature in (
        'XpAppText text(const std::string& value)', 'const char* pathText(XpAppText value)', 'std::string owned(XpAppText value)'))
    source = replace_once(source, '// ACTUAL_TEXT_HELPERS', helpers)
    source = replace_once(source, '// ACTUAL_CSTR_HELPER', definition(app, 'const char* cstr(XpAppText value)'))
    css = (ROOT / 'lib/Epub/Epub/css/CssPlatform.cpp').read_text()
    source = replace_once(source, '// ACTUAL_CSS_DIAGNOSTIC', definition(css, 'void cssDiagnostic(void*, uint32_t event, uint32_t a, uint32_t b, uint32_t, uint32_t)'))
    source = replace_once(source, '// ACTUAL_TRANSACTION_DIAGNOSTIC', definition(css, 'static void diagnostic(void* raw, uint32_t event, uint32_t argument)'))
    fields = ('file_open_read', 'file_open_write', 'file_read', 'file_write', 'file_close', 'file_seek', 'file_size', 'log', 'log_enabled')
    leaves = '\n'.join('  value.' + name + ' = ' + native_leaf(native, name) + ';' for name in fields)
    leaves += '\n  value.path_rename = [](XpAppText from, XpAppText to) -> uint8_t { ++renames; return (' + native_leaf(native, 'path_rename') + ')(from, to); };'
    leaves += '\n  value.path_remove = [](XpAppText path) -> uint8_t { removed.push_back(owned(path)); return (' + native_leaf(native, 'path_remove') + ')(path); };'
    source = replace_once(source, '// ACTUAL_LEAVES', leaves)
    source = replace_once(source, '// ACTUAL_ALLOCATOR_LEAVES', '\n'.join('  value.' + name + ' = ' + native_leaf(app, name) + ';' for name in ('buffer_allocate', 'buffer_release', 'path_exists')))
    output = Path(os.environ.get('XP_BOOK_CSS_EVIDENCE_DIR') or tempfile.mkdtemp(prefix='book-css-'))
    output.mkdir(parents=True, exist_ok=True)
    cpp = output / 'oracle.cpp'; cpp.write_text(source)
    binary = output / 'oracle'
    command = [os.environ.get('CXX', 'c++'), '-std=c++20', '-O0', '-g', '-UNDEBUG',
               '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-DXP_FFI_HOST_FAULT_INJECTION',
               '-I', str(ROOT / 'lib/Memory'), '-I', str(HERE), '-I', str(ROOT / 'test/ui_runtime'),
               '-I', str(ROOT / 'test/chapter_html_slim_parser/stubs'), str(cpp), *rust_link_flags(), '-o', str(binary)]
    built = subprocess.run(command, capture_output=True, timeout=120)
    (output / 'compile.log').write_bytes(built.stdout + built.stderr)
    if built.returncode: raise RuntimeError(built.stderr.decode())
    ran = subprocess.run([str(binary), str(output / 'files')], capture_output=True, timeout=120)
    (output / 'run.log').write_bytes(ran.stdout + ran.stderr)
    if ran.returncode: raise RuntimeError(ran.stderr.decode())
    (output / 'results.json').write_text(json.dumps(dict(
        command=command, returncode=ran.returncode,
        diagnostic_cases=128,
        native_sha256=hashlib.sha256(native.encode()).hexdigest(),
        css_native_sha256=hashlib.sha256(css.encode()).hexdigest(),
        generated_source_sha256=hashlib.sha256(source.encode()).hexdigest(),
        archive_sha256=hashlib.sha256(Path(next(arg for arg in command if arg.endswith('/libxp_ffi.a'))).read_bytes()).hexdigest(),
    ), indent=2) + '\n')
    print('Actual native close/Rust CSS: failed-close persistence, once-only destruction, success/partial/restore/zero-rule/OOM cleanup and 128 native/Rust diagnostic cases pass.')


if __name__ == '__main__': main()
