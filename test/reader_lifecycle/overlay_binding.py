"""Extract reachable production callback cases for isolated parent operations.

The fixtures omit unrelated child-screen types. Every retained case is verbatim;
unexpected callbacks assert instead of silently supplying a fixture default.
"""
import re
from extract import definition

TRANSITION_QUERIES = {'HAS_TOOLBAR', 'HAS_SECTION', 'PAGE_STALE', 'TOUCH', 'CURRENT_TOC', 'IS_A4', 'IS_XTEINK'}
TRANSITION_COMMANDS = {'LOCK', 'UNLOCK', 'SETTLE', 'RESTORE', 'STORE', 'RENDER', 'PUSH', 'UPDATE',
                       'DISMISS_POPUP', 'DISCARD', 'TOOLBAR_BEGIN', 'RESET_HOME', 'CREATE_TOOLBAR',
                       'RESET_NAVIGATION', 'BUILD_MORE', 'DROP_TOOLBAR', 'FORCE_COUNTER', 'DISPLAY_FAST'}
NEW_QUERIES = TRANSITION_QUERIES - {'HAS_TOOLBAR'}
NEW_COMMANDS = {'RESET_HOME', 'CREATE_TOOLBAR', 'RESET_NAVIGATION', 'BUILD_MORE', 'DROP_TOOLBAR', 'FORCE_COUNTER', 'DISPLAY_FAST'}

def binding(source, transitions):
    text = definition(source, 'void EpubReaderActivity::runOverlayPolicy(')
    for method, group, selected in [('uint32_t query(', 'QUERY', TRANSITION_QUERIES if transitions else NEW_QUERIES),
                                     ('uint32_t command(', 'COMMAND', TRANSITION_COMMANDS if transitions else NEW_COMMANDS)]:
        body = definition(text, method)
        matches = list(re.finditer(r'        case XP_UI_READER_OVERLAY_' + group + r'_(\w+):', body))
        end = body.rindex('      }')
        pieces = [body[:matches[0].start()]]
        for i, match in enumerate(matches):
            keep = (match[1] in selected) == transitions
            if keep:
                pieces.append(body[match.start():matches[i+1].start() if i+1 < len(matches) else end])
        pieces.append('        default: assert(false); return 0;\n' + body[end:])
        text = text.replace(body, ''.join(pieces))
    if transitions:
        text = re.sub(r'^    std::optional<decltype\(.*\n', '', text, flags=re.M)
        text = re.sub(r'^    ReaderToolbarUi::Routed .*\n', '', text, flags=re.M)
        text = re.sub(r'^    uint32_t pressed = .*\n', '', text, flags=re.M)
        text = re.sub(r'^  static_assert\(.*\n', '', text, flags=re.M)
        text = text.replace('  assert(!context.item);\n', '')
    return text
