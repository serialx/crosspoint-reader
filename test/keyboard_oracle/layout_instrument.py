"""Extract the actual application table and SDK callbacks for host checks."""
from pathlib import Path
import re
import sys


def function(source, signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def bindings(root):
    header = (root / 'src/activities/util/KeyboardLayoutSet.h').read_text()
    header = header.replace('#include <I18n.h>', '#include <I18nKeys.h>').replace('#pragma once\n', '')
    source = (root / 'src/activities/util/KeyboardEntryActivity.cpp').read_text()
    callback = function(source, 'XpUiKeyboardLayouts KeyboardEntryActivity::layouts(')
    return ('#include <xp_ui_keyboard.h>\n' + header + '\nnamespace fui = freeink::ui;\n'
            'struct ProductionKeyboardLayouts { static XpUiKeyboardLayouts layouts(); };\n' +
            callback.replace('KeyboardEntryActivity::', 'ProductionKeyboardLayouts::') + '\n')


if __name__ == '__main__':
    root, out = map(Path, sys.argv[1:])
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(bindings(root))
