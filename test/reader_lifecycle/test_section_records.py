"""Actual Rust Section chunks with native DEFAULT leaves and forced allocator movement."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from test_popup_resource import native_leaf, replace_once
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'test'))
from rust_host import rust_link_flags


def main():
    app = (ROOT / 'src/activities/RustAppResources.cpp').read_text()
    source = (ROOT / 'test/reader_lifecycle/section_records_oracle.cpp').read_text()
    allocators = '\n'.join('  value.' + name + ' = ' + native_leaf(app, name) + ';'
                           for name in ('buffer_allocate', 'buffer_release'))
    source = replace_once(source, '// ACTUAL_ALLOCATOR_LEAVES', allocators)
    output = Path(os.environ.get('XP_SECTION_RECORDS_EVIDENCE_DIR') or tempfile.mkdtemp(prefix='section-records-'))
    output.mkdir(parents=True, exist_ok=True)
    cpp = output / 'oracle.cpp'
    cpp.write_text(source)
    flags = rust_link_flags()
    flags.remove(str(ROOT / 'test/cmake/xp_ffi_host.cpp'))
    command = [os.environ.get('CXX', 'c++'), '-std=c++20', '-O0', '-g', '-UNDEBUG',
               '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
               '-I', str(ROOT / 'test/ui_runtime'), '-I', str(ROOT / 'test/reader_lifecycle'), str(cpp), *flags, '-o', str(output / 'oracle')]
    checks = []
    for name, cmd in [('compile', command), ('run', [str(output / 'oracle')])]:
        result = subprocess.run(cmd, capture_output=True)
        (output / (name + '.log')).write_bytes(result.stdout + result.stderr)
        checks.append(dict(name=name, command=cmd, returncode=result.returncode))
        (output / 'results.json').write_text(json.dumps(checks, indent=2) + '\n')
        if result.returncode:
            raise RuntimeError((result.stdout + result.stderr).decode())
    print((output / 'run.log').read_text(), end='')


if __name__ == '__main__':
    main()
