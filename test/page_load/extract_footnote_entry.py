"""Compile production metadata scopes and instrument selector access in the host harness."""
import pathlib
import re
import sys

source = pathlib.Path(sys.argv[1]).read_text()

def method(name):
    start = source.index('void EpubReaderActivity::' + name + '(')
    end = source.index('\nvoid EpubReaderActivity::', start + 1)
    return source[start:end]

entry = method('openFootnoteSelect')
tap_start = source.index('  // Link taps take priority')
tap_end = source.index('  const bool touchMenu', tap_start)
tap = source[tap_start:tap_end]
menus = []
for name in ('openReaderMenu', 'buildMoreActions'):
    body = method(name)
    start = body.index('  bool hasFootnotes;')
    end = body.index('\n  }', start) + len('\n  }')
    menus.append(body[start:end])

# New input consumers must be added to this executable harness too.
assert source.count('currentPageMetadata.') == sum(
    text.count('currentPageMetadata.') for text in [entry, tap, *menus]
) + 2  # renderBook's clear and assign are protected by ActivityManager.

pathlib.Path(sys.argv[2]).write_text(
    '#include "FootnoteReader.h"\n'
    '#include "activities/reader/EpubReaderFootnoteSelectActivity.h"\n'
    '#include "activities/reader/EpubReaderUtils.h"\n'
    '#include "CrossPointSettings.h"\n'
    'namespace haptic_feedback { void touchAction() {} }\n' + entry + '\n'
    'void EpubReaderActivity::testLinkTap() {\n' + tap + '}\n'
    'bool EpubReaderActivity::testMenuAvailability() {\n' + menus[0] + '\nreturn hasFootnotes;\n}\n'
    'bool EpubReaderActivity::testMoreAvailability() {\n' + menus[1] + '\nreturn hasFootnotes;\n}\n')

selector = pathlib.Path(sys.argv[3]).read_text()
selector = selector.replace('#include "EpubReaderFootnoteSelectActivity.h"',
                            '#include "activities/reader/EpubReaderFootnoteSelectActivity.h"')
for accessor in ('footnote', 'link'):
    selector = selector.replace('page->' + accessor + '(',
                                '(RenderLock::requireHeld(), page)->' + accessor + '(')
selector = re.sub(r'\bselected\b', 'checkedSelection(selected)', selector)
pathlib.Path(sys.argv[4]).write_text(selector)
