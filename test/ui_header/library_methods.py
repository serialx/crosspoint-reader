"""Compose the production Library painter and its frozen parent for pixel comparison."""
from pathlib import Path
import sys

root, output = map(Path, sys.argv[1:])
source = (root / "src/activities/library/LibraryListActivity.cpp").read_text()
start = source.index("void LibraryListActivity::paintHeader(")
end = source.index("\nXpUiPaint LibraryListActivity::rustResources()", start)
parent = (root / "test/ui_header/native_oracle/library-paint-header.txt").read_text()
output.write_text(
    source[start:end].replace("LibraryListActivity::paintHeader", "HeaderOwner::paintHeader")
    + parent.replace("LibraryListActivity::paintHeader", "HeaderOwner::paintBaseline")
)
