"""Compose actual Section/parser/ProgressMapper borrowers with the Rust book."""
from pathlib import Path
import struct
import zipfile
from book_facade_composition import facade_source, compile_command
from test_popup_resource import definition, replace_once


def section_source(root):
    source = facade_source(root)
    source = replace_once(source, definition(source, 'struct ImageBlock {') + ';', '')
    source = replace_once(source, definition(source, 'void ImageBlock::setExtractor('), '')
    includes = ('#include <ProgressMapper.h>\n#include <ChapterXPathResolver.h>\n'
                '#include <Epub/blocks/ImageBlock.h>\n#include <Epub/Section.h>\n'
                '#include <Epub/Page.h>\n#include <GfxRenderer.h>\n')
    return includes + source + (root / 'test/reader_lifecycle/book_section_oracle.cpp').read_text()


def section_command(root, work, source, binary, archive=None):
    command = compile_command(root, work, source, binary, archive=archive)
    includes = [
        'lib/KOReaderSync', 'lib/Epub', 'test/section_golden/stubs',
        'test/section_golden', 'lib/EpdFont', 'lib/GfxRenderer', 'lib/AsciiText',
        'lib/MiniBidi', 'lib/XmlParserUtils', 'lib', 'src', 'lib/Epub/Epub/parsers',
    ]
    # Keep common HalStorage first and actual Epub before the Section oracle stubs.
    position = command.index('-I') + 2
    command[position:position] = [part for path in includes for part in ('-I', str(root / path))]
    sources = [
        'lib/KOReaderSync/ProgressMapper.cpp', 'lib/KOReaderSync/ChapterXPathResolver.cpp',
        'test/section_golden/ImageDecoderStub.cpp', 'lib/Epub/Epub/SectionPlatform.cpp',
        'lib/Epub/Epub/Page.cpp', 'lib/Epub/Epub/blocks/TextBlock.cpp',
        'lib/Epub/Epub/blocks/ImageBlock.cpp', 'lib/Epub/Epub/Paragraph.cpp',
        'lib/Epub/Epub/CjkBreaks.cpp', 'lib/Epub/Epub/parsers/ChapterPlatform.cpp',
        'lib/Epub/Epub/converters/ImageDimsProbe.cpp',
        'lib/Epub/Epub/converters/ImageToFramebufferDecoder.cpp',
        'lib/Epub/Epub/htmlEntities.cpp', 'lib/Epub/Epub/hyphenation/Hyphenator.cpp',
        'lib/MiniBidi/RustBidi.cpp',
    ]
    position = command.index('-o')
    command[position:position] = [str(root / path) for path in sources]
    return command


def section_fixture(work):
    cache = work / 'fixture'
    cache.mkdir(exist_ok=True)
    (cache / 'sections').mkdir(exist_ok=True)
    (cache / 'source.css').write_text('p { margin-top: 2em; }\n')
    paragraph = b'<p>A chapter with enough text to retain the actual parser between render ticks and page builds.</p>'
    html = b'<html><body><p id="anchor">Start</p>' + paragraph * 1000 + b'</body></html>'

    def string(value):
        return struct.pack('<I', len(value)) + value

    metadata = b''.join(map(string, [b'Title', b'Author', b'en', b'', b'a.xhtml']))
    table = 9 + len(metadata)
    rows = [string(b'a.xhtml') + struct.pack('<Ih', len(html), 0),
            string(b'b.xhtml') + struct.pack('<Ih', len(html) + 6, 0),
            string(b'Chapter') + string(b'a.xhtml') + string(b'anchor') + struct.pack('<Bh', 1, 0)]
    position = table + 12
    offsets = []
    for row in rows:
        offsets.append(position)
        position += len(row)
    (cache / 'book.bin').write_bytes(struct.pack('<BIHH', 10, table, 2, 1) + metadata +
                                   struct.pack('<III', *offsets) + b''.join(rows))
    with zipfile.ZipFile(work / 'source.epub', 'w', compression=zipfile.ZIP_STORED) as archive:
        archive.writestr('a.xhtml', html)
        archive.writestr('b.xhtml', b'image!')
    return work / 'source.epub', cache
