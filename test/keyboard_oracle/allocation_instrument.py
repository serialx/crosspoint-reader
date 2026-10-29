"""Compile the production allocation/retry path with an injected allocator."""
import pathlib
import sys
root, out = map(pathlib.Path, sys.argv[1:])
s = (root / 'src/activities/util/KeyboardEntryActivity.cpp').read_text()
parts = []
for signature in ['bool KeyboardEntryActivity::Bytes::grow(', 'XpUiEffect KeyboardEntryActivity::updateRust(', 'void KeyboardEntryActivity::applyRustEffect(', 'bool KeyboardEntryActivity::buildRustScene(', 'void KeyboardEntryActivity::rustPublished(', 'void KeyboardEntryActivity::loop(']:
    start = s.index(signature)
    end = s.index('{', start) + 1
    depth = 1
    while depth:
        depth += (s[end] == '{') - (s[end] == '}')
        end += 1
    parts.append(s[start:end])
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text('\n'.join(parts))

h = (root / 'src/activities/util/KeyboardEntryActivity.h').read_text()
a = h.index('  struct Arena {')
b = h.index('\n  };', a) + len('\n  };')
(out.parent / 'keyboard-arena.inc').write_text(h[a:b])
