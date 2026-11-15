"""Compile the two production ClipSelection constructors unchanged."""
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'test/reader_lifecycle'))
from test_popup_resource import definition

source = (root / 'src/activities/reader/ClipSelectionActivity.cpp').read_text()
anchor = 'ClipSelectionActivity::ClipSelectionActivity('
assert source.count(anchor) == 2
split = source.index(anchor, source.index(anchor) + len(anchor))
first = definition(source[:split], anchor)
second = definition(source[split:], anchor)
Path(sys.argv[1]).write_text('#include "activities/reader/ClipSelectionActivity.h"\n' + first + '\n' + second + '\n')
