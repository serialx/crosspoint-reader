"""Exercise native book borrowers across actual parser ticks and sync mappings."""
from pathlib import Path
import argparse
import os
import hashlib
import json
import subprocess
import tempfile

from book_section_composition import section_command, section_fixture, section_owner_source

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', default=os.environ.get('XP_TEST_RUST_ARCHIVE'))
    args = parser.parse_args()
    if args.archive is None:
        parser.error('provide the CMake host archive with app-test and chapter-test enabled')
    source = section_owner_source(ROOT) + (ROOT / 'test/reader_lifecycle/book_section_oracle.cpp').read_text()
    with tempfile.TemporaryDirectory(prefix='reader-book-section-') as directory:
        work = Path(directory)
        cpp, binary = work / 'section.cpp', work / 'section'
        cpp.write_text(source)
        subprocess.run(section_command(ROOT, work, cpp, binary, archive=args.archive), check=True)
        book, cache = section_fixture(work)
        result = subprocess.run([str(binary), str(book), str(cache)], capture_output=True)
        assert result.returncode == 0, (result.returncode, result.stderr.decode(errors='replace')[-5000:])
        summary = [line for line in result.stdout.decode().splitlines() if line.startswith('actual Section/')]
        assert len(summary) == 1
        print(summary[0])
    print(json.dumps(dict(source_sha256=hashlib.sha256(source.encode()).hexdigest(),
                          production=['ReaderBook', 'Epub facade', 'ReaderSection', 'Chapter',
                                      'Rust CSS', 'native Page', 'cache-only Section', 'ProgressMapper', 'ChapterXPathResolver'],
                          policy_hooks=0, limits=['host storage and font/renderer fakes',
                                                 'image decoding disabled in the Section workload',
                                                 'serialized render thread ticks; not a hardware timing measurement'])))


if __name__ == '__main__':
    main()
