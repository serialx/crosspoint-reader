"""Compare the actual Rust spec conversion to the frozen parent native functions."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys
import tempfile
from test_popup_resource import definition
ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).parent
sys.path.insert(0, str(ROOT / 'test'))
from rust_host import rust_link_flags


def main():
    frozen = (HERE / 'render_spec_parent.inc').read_text()
    pins = json.loads((HERE / 'render_spec_parent.json').read_text())
    for signature, key in [('ReaderRenderSpec renderSpec(', 'renderSpec_sha256'),
                           ('std::array<uint32_t, 20> headerFields(', 'headerFields_sha256')]:
        assert hashlib.sha256(definition(frozen, signature).encode()).hexdigest() == pins[key]
    output = Path(os.environ.get('XP_SECTION_SPEC_EVIDENCE_DIR') or tempfile.mkdtemp(prefix='section-render-spec-'))
    output.mkdir(parents=True, exist_ok=True)
    binary = output / 'oracle'
    command = ['c++', '-std=c++20', '-O0', '-g', '-UNDEBUG', '-I', str(ROOT / 'lib/Epub'),
               '-I', str(ROOT / 'test/book_io/epub-stubs'), '-I', str(ROOT / 'test/chapter_html_slim_parser/stubs'),
               str(HERE / 'render_spec_oracle.cpp'), *rust_link_flags(), '-o', str(binary)]
    checks = []
    for name, args in [('compile', command), ('run', [str(binary)])]:
        process = subprocess.run(args, capture_output=True)
        (output / (name + '.log')).write_bytes(process.stdout + process.stderr)
        checks.append({'name': name, 'command': args, 'returncode': process.returncode})
        (output / 'results.json').write_text(json.dumps({'parent': pins['parent'], 'checks': checks}, indent=2) + '\n')
        if process.returncode:
            raise RuntimeError((process.stdout + process.stderr).decode(errors='replace')[-3000:])
    print((output / 'run.log').read_text().strip())


if __name__ == '__main__':
    main()
