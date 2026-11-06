"""Compile the frozen pre-owner allocation/retry baseline with an injected allocator."""
import pathlib
import sys
root, out = map(pathlib.Path, sys.argv[1:])
s = (root / 'test/keyboard_oracle/native_oracle/KeyboardEntryActivity.cpp.txt').read_text()
parts = []
for signature in ['bool KeyboardEntryActivity::Bytes::grow(', 'XpUiEffect KeyboardEntryActivity::updateRust(', 'void KeyboardEntryActivity::applyRustEffect(', 'bool KeyboardEntryActivity::buildRustScene(', 'void KeyboardEntryActivity::rustPublished(', 'void KeyboardEntryActivity::loop(', 'void KeyboardEntryActivity::reconcileInput(']:
    start = s.index(signature)
    end = s.index('{', start) + 1
    depth = 1
    while depth:
        depth += (s[end] == '{') - (s[end] == '}')
        end += 1
    parts.append(s[start:end])
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text('\n'.join(parts))

h = (root / 'test/keyboard_oracle/native_oracle/KeyboardEntryActivity.h.txt').read_text()
a = h.index('  struct Arena {')
b = h.index('\n  };', a) + len('\n  };')
(out.parent / 'keyboard-arena.inc').write_text(h[a:b])
