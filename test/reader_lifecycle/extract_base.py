"""Extract the shared production reader lifecycle against deterministic leaves."""
import pathlib
import sys
import re
from extract import definition

repo, output = map(pathlib.Path, sys.argv[1:])
source = (repo / "src/activities/reader/ReaderActivity.cpp").read_text()
names = (
    "void ReaderActivity::rememberBookOnceRendered(",
    "void ReaderActivity::clearEndOfBookOptionsIfNeeded(",
    "bool ReaderActivity::endOfBookMenuActive(",
    "bool ReaderActivity::handleEndOfBookMenu(",
    "bool ReaderActivity::handleEndOfBookPageTurn(",
    "void ReaderActivity::loop(",
    "void ReaderActivity::render(",
    "bool ReaderActivity::handleForcedRefresh(",
    "void ReaderActivity::onEnter(",
    "void ReaderActivity::onExit(",
    "void ReaderActivity::flushReaderSession(",
)
output.write_text("\n".join(definition(source, name) for name in names))

header = (repo / "src/activities/reader/EndOfBookOptions.h").read_text()
action = re.search(r"enum class Action \{[^}]+\};", header).group(0)
output.with_name("base_action.inc").write_text(action + "\n")
