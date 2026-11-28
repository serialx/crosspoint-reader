"""Keep large activity frames outside shared App dispatchers.

This structural check complements linked target frame/chain measurements.
"""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2] / 'rust/crates/xp-ffi/src/app'
BOUNDARIES = {
    'epub/render_scope.rs': 'render',
    'epub/loop_owner.rs': 'loop_reader',
    'epub/base.rs': 'enter',
    'epub/child_result.rs': 'child_result',
    'epub/ownership.rs': 'exit_owned',
    'home.rs': 'enter',
}


def check(source, name):
    pattern = rf'#\[inline\(never\)\]\s+pub(?:\([^)]*\))?\s+(?:unsafe\s+)?fn {name}\('
    assert len(re.findall(pattern, source)) == 1, ('missing stack boundary', name)


for path, name in BOUNDARIES.items():
    source = (ROOT / path).read_text()
    check(source, name)
    removed = source.replace('#[inline(never)]', '#[inline]')
    try:
        check(removed, name)
    except AssertionError:
        pass
    else:
        raise AssertionError(('removed boundary survived', path, name))
print('PASS six shared-dispatch stack boundaries and six attribute-removal checks')
