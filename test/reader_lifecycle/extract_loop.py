"""Compile the full production reader loop against deterministic resource leaves."""
import pathlib
import re
import sys
from extract import definition

repo = pathlib.Path(sys.argv[1])
source = (repo / "src/activities/reader/EpubReaderActivity.cpp").read_text()
utils = (repo / "src/activities/reader/ReaderUtils.h").read_text()
constants = re.findall(r"constexpr unsigned long (?:GO_HOME_MS|GO_BACK_OR_HOME_MS|BOOKMARK_MESSAGE_DURATION_MS) = [^;]+;", utils)
assert len(constants) == 3
pathlib.Path(sys.argv[2]).write_text("namespace ReaderUtils {\n" + "\n".join(constants) + "\n}\n" + definition(source, "void EpubReaderActivity::loop()") + "\n")
