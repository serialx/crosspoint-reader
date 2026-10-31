"""Extract production input policy for lazy-probe ordering traces."""
import pathlib
import sys
from extract import definition

repo, output = map(pathlib.Path, sys.argv[1:])
source = (repo / "src/activities/reader/ReaderUtils.h").read_text()
names = ["inline bool gestureAllowsSwipe(", "inline bool gestureAllowsTap(",
         "inline bool isRtlBookLanguage(", "inline PageTurnResult detectPageTurn(",
         "inline TouchPageTurn detectTouchPageTurn(", "inline bool isTouchMenuTap(",
         "inline bool isTouchMenuGesture(", "inline bool handleBackNavigation("]
if "inline XpUiReaderInputServices inputServices(" in source:
    names = ["inline XpUiReaderInputConfig inputConfig(", "inline XpUiReaderInputServices inputServices("] + names
names = [name for name in names if name in source]
output.write_text("namespace ReaderUtils {\n" + "\n".join(definition(source, name) for name in names) + "\n}\n")
