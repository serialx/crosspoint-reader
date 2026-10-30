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
tap_start = source.index('        case XP_UI_READER_LOOP_COMMAND_MATCH_LINK:')
tap_end = source.index('        case XP_UI_READER_LOOP_COMMAND_BACK_NAVIGATION:', tap_start)
tap_commands = source[tap_start:tap_end]
point_start = source.index('        int a = 0, b = 0;', source.index('[](void* p, uint8_t q, int32_t* x'))
point_end = source.index('      }};', point_start)
tap_point = source[point_start:point_end]
tap = """
  struct Context {
    EpubReaderActivity& o;
    std::string href;
    uint32_t command(uint8_t cmd, int32_t a, int32_t b) {
      switch (cmd) {
""" + tap_commands + """
        case XP_UI_READER_LOOP_COMMAND_BACK_NAVIGATION: return 1;
        default: return 0;
      }
      return 0;
    }
  } context{*this, {}};
  const XpUiReaderLoopServices services{
    &context,
    [](void*, uint8_t) -> uint32_t { return 0; },
    [](void*, uint8_t, uint32_t) {},
    [](void*, uint8_t query) -> uint32_t {
      return query == XP_UI_READER_LOOP_QUERY_HAS_EPUB ||
             query == XP_UI_READER_LOOP_QUERY_TOUCH_CONTROLS ||
             query == XP_UI_READER_LOOP_QUERY_HAS_TOUCH;
    },
    [](void* p, uint8_t c, int32_t a, int32_t b) {
      return static_cast<Context*>(p)->command(c, a, b);
    },
    [](void*) -> XpUiReaderTurnInput { return {}; },
    [](void*) -> XpUiReaderTurnInput { return {}; },
    [](void* p, uint8_t q, int32_t* x, int32_t* y) -> uint8_t {
      auto& o = static_cast<Context*>(p)->o;
""" + tap_point + """
    }};
  xp_ui_reader_loop_run(&services);
"""
# Execute the Rust parent policy while retaining the actual native metadata leaf.
parent_start = source.index('        case XP_UI_READER_PARENT_COMMAND_FOOTNOTES_PRESENT:')
parent_end = source.index('        case XP_UI_READER_PARENT_COMMAND_SHOW_MENU:', parent_start)
parent_metadata = source[parent_start:parent_end]
parent = """
  struct Context { EpubReaderActivity& o; std::optional<RenderLock> lock; bool available = false; } context{*this, {}};
  const XpUiReaderParentServices services{
    &context,
    [](void*, uint8_t) -> uint32_t { return 0; },
    [](void*, uint8_t, uint32_t) {},
    [](void* p, uint8_t command, int32_t a, int32_t b) -> uint32_t {
      auto& c = *static_cast<Context*>(p); auto& o = c.o;
      switch (command) {
        case XP_UI_READER_PARENT_COMMAND_LOCK: c.lock.emplace(); break;
        case XP_UI_READER_PARENT_COMMAND_UNLOCK: c.lock.reset(); break;
""" + parent_metadata + """
        case XP_UI_READER_PARENT_COMMAND_SHOW_MENU: c.available = b != 0; break;
        case XP_UI_READER_PARENT_COMMAND_BUILD_ITEMS: c.available = a != 0; break;
        default: break;
      }
      return 0;
    }};
  xp_ui_reader_menu_parent(&services, OPERATION, 0);
  assert(!context.lock);
  return context.available;
"""
menus = [parent.replace('OPERATION', operation) for operation in
         ['XP_UI_READER_PARENT_OPERATION_OPEN', 'XP_UI_READER_PARENT_OPERATION_BUILD_MORE']]

# One shared menu leaf serves both parent operations; render clear/assign are locked by ActivityManager.
assert source.count('currentPageMetadata.') == sum(
    text.count('currentPageMetadata.') for text in [entry, tap, parent_metadata]
) + 2

pathlib.Path(sys.argv[2]).write_text(
    '#include "FootnoteReader.h"\n'
    '#include <xp_ui_reader.h>\n#include <optional>\n#include <cassert>\n'
    '#include "activities/reader/EpubReaderFootnoteSelectActivity.h"\n'
    '#include "activities/reader/EpubReaderUtils.h"\n'
    '#include "CrossPointSettings.h"\n'
    'namespace haptic_feedback { void touchAction() {} }\n' + entry + '\n'
    'void EpubReaderActivity::testLinkTap() {\n' + tap + '}\n'
    'bool EpubReaderActivity::testMenuAvailability() {\n' + menus[0] + '\n}\n'
    'bool EpubReaderActivity::testMoreAvailability() {\n' + menus[1] + '\n}\n')

selector = pathlib.Path(sys.argv[3]).read_text()
selector = selector.replace('#include "EpubReaderFootnoteSelectActivity.h"',
                            '#include "activities/reader/EpubReaderFootnoteSelectActivity.h"')
for accessor in ('footnote', 'link'):
    selector = selector.replace('page->' + accessor + '(',
                                '(RenderLock::requireHeld(), page)->' + accessor + '(')
selector = selector.replace('state.selected', 'checkedModelSelection(state.selected)')
pathlib.Path(sys.argv[4]).write_text(selector)
