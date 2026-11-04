"""Compare the dormant real-book Rust Chapter driver with native Section output."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import tempfile
import struct
import zipfile
from book_section_composition import section_source, section_command, section_fixture
from test_popup_resource import definition, replace_once
ROOT = Path(__file__).resolve().parents[2]


def main():
    source = section_source(ROOT).split('#include <mutex>')[0]
    oracle = (ROOT / 'test/reader_lifecycle/section_driver_oracle.cpp').read_text()
    header = definition((ROOT / 'lib/Epub/Epub/SectionPlatform.cpp').read_text(),
                        'std::array<uint32_t, 20> headerFields(')
    chapter = (ROOT / 'lib/Epub/Epub/parsers/ChapterPlatform.cpp').read_text()
    metrics = chapter[chapter.index('const XpParaMetrics ChapterPageAssembler::metrics_'):chapter.index('const XpChapterSink ChapterPageAssembler::sink_')]
    metrics = replace_once(metrics, 'ChapterPageAssembler::metrics_', 'driverMetrics')
    metrics = replace_once(metrics, 'driverMetrics{\n    nullptr,', 'driverMetrics{\n    &driverRenderer,')
    helpers = '\n'.join(definition(chapter, signature) for signature in (
        'int32_t ChapterPageAssembler::textAdvance(', 'void ChapterPageAssembler::prepareText(',
        'uint8_t startsRtl(', 'uint8_t visualOrder('))
    helpers = helpers.replace('ChapterPageAssembler::textAdvance', 'textAdvance').replace('ChapterPageAssembler::prepareText', 'prepareText')
    metrics = (helpers + '\n' + metrics).replace('metricRenderer(', 'physicalRenderer(')
    oracle = replace_once(oracle, '// NATIVE_METRICS', metrics)
    source += replace_once(oracle, '// ACTUAL_HEADER_FIELDS', header)
    output = Path(os.environ.get('XP_SECTION_DRIVER_EVIDENCE_DIR') or tempfile.mkdtemp(prefix='section-driver-'))
    output.mkdir(parents=True, exist_ok=True)
    cpp, binary = output / 'oracle.cpp', output / 'oracle'
    cpp.write_text(source)
    book, cache = section_fixture(output)
    rich = output / 'rich'
    rich.mkdir(exist_ok=True)
    rich_book, rich_cache = section_fixture(rich)
    html = (b"<!DOCTYPE html SYSTEM 'unused'><html><body><p id='anchor'>Start &nbsp; " +
            b'&' + b'x' * 1100 + b';</p>' +
            b'<table><tr><td>first cell</td><td>second cell</td></tr></table>' * 5 +
            b'<p>Text before image.</p><img src="../tiny%2Epng"/><img src="unsupported.svg"/>' +
            b'<p>Body text after table and image, with enough lines for several pages.</p>' * 80 +
            b'</body></html>')
    with zipfile.ZipFile(rich_book, 'w', compression=zipfile.ZIP_STORED) as archive:
        archive.writestr('a.xhtml', html)
        archive.writestr('b.xhtml', b'image!')
        archive.writestr('tiny.png', b'\x89PNG\r\n\x1a\n' + struct.pack('>I', 13) + b'IHDR' + struct.pack('>II', 2, 3))
    metadata = bytearray((rich_cache / 'book.bin').read_bytes())
    table = struct.unpack_from('<I', metadata, 1)[0]
    for index, cumulative in enumerate((len(html), len(html) + 6)):
        row = struct.unpack_from('<I', metadata, table + index * 4)[0]
        length = struct.unpack_from('<I', metadata, row)[0]
        struct.pack_into('<I', metadata, row + 4 + length, cumulative)
    (rich_cache / 'book.bin').write_bytes(metadata)
    checks = []
    commands = [('compile', section_command(ROOT, output, cpp, binary, os.environ.get('XP_TEST_RUST_ARCHIVE')))]
    for case, case_book, case_cache in [('plain', book, cache), ('rich', rich_book, rich_cache)]:
        commands += [(case + '-' + mode, [str(binary), str(case_book), str(case_cache), mode]) for mode in ('complete', 'partial')]
    for name, command in commands:
        result = subprocess.run(command, capture_output=True)
        (output / (name + '.log')).write_bytes(result.stdout + result.stderr)
        checks.append(dict(name=name, command=command, returncode=result.returncode))
        (output / 'results.json').write_text(json.dumps(dict(checks=checks, source_sha256=hashlib.sha256(source.encode()).hexdigest(),
            production=['ReaderBook', 'ReaderSection storage', 'BuildOwner', 'BuildServices', 'Chapter', 'Section',
                        'ChapterHtmlSlimParser', 'Page', 'ProgressMapper', 'native physical file/resource leaves'], policy_hooks=0,
            limits=['host storage and font fakes', 'dormant driver composition; session switch is CP3']), indent=2) + '\n')
        if result.returncode:
            raise RuntimeError((result.stdout + result.stderr).decode(errors='replace')[-6000:])
    for name, _ in commands[1:]:
        print(name + ': ' + '\n'.join(line for line in (output / (name + '.log')).read_text().splitlines() if line.startswith('Section driver:')))


if __name__ == '__main__':
    main()
