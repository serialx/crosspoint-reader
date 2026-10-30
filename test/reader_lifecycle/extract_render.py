"""Extract production render sequencing without copying it into the test."""
import pathlib
import sys
from extract import definition

repo, output = map(pathlib.Path, sys.argv[1:])
reader = (repo / "src/activities/reader/EpubReaderActivity.cpp").read_text()
utils = (repo / "src/activities/reader/ReaderUtils.h").read_text()
methods = "namespace ReaderUtils {\n" + "\n".join(
    definition(utils, signature) for signature in (
        "inline void displayWithRefreshCycle(", "inline void displayBaseWithRefreshCycle(")) + "\n}\n"
methods += definition(reader, "void EpubReaderActivity::renderContents(")
output.write_text(methods + "\n")
