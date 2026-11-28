"""Round-trip actual ReaderSection output through its storage adapter and native readers."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import tempfile
from book_section_composition import section_owner_source, section_command, section_fixture
from test_popup_resource import definition, replace_once
ROOT = Path(__file__).resolve().parents[2]


def main():
    source = section_owner_source(ROOT) + (ROOT / 'test/reader_lifecycle/section_storage_oracle.cpp').read_text()
    output = Path(os.environ.get('XP_SECTION_STORAGE_EVIDENCE_DIR') or tempfile.mkdtemp(prefix='section-storage-'))
    output.mkdir(parents=True, exist_ok=True)
    cpp, binary = output / 'oracle.cpp', output / 'oracle'
    cpp.write_text(source)
    book, cache = section_fixture(output)
    checks = []
    for name, command in [('compile', section_command(ROOT, output, cpp, binary, os.environ.get('XP_TEST_RUST_ARCHIVE'))),
                          ('run', [str(binary), str(book), str(cache)])]:
        result = subprocess.run(command, capture_output=True)
        (output / (name + '.log')).write_bytes(result.stdout + result.stderr)
        checks.append(dict(name=name, command=command, returncode=result.returncode))
        (output / 'results.json').write_text(json.dumps(dict(checks=checks, source_sha256=hashlib.sha256(source.encode()).hexdigest(),
            production=['ReaderBook', 'ReaderSection build/storage', 'cache-only Section', 'Chapter',
                        'Page', 'ProgressMapper', 'native physical file/resource leaves'], policy_hooks=0,
            limits=['host storage and font fakes; image rendering disabled', 'actual reader owner; no legacy fixture']), indent=2) + '\n')
        if result.returncode:
            raise RuntimeError((result.stdout + result.stderr).decode(errors='replace')[-6000:])
    print('\n'.join(line for line in (output / 'run.log').read_text().splitlines() if line.startswith('Section storage roundtrip:')))


if __name__ == '__main__':
    main()
