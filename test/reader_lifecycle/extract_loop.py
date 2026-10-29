"""Compile the full production reader loop against deterministic resource leaves."""
import pathlib
import sys
from extract import definition

repo = pathlib.Path(sys.argv[1])
source = (repo / "src/activities/reader/EpubReaderActivity.cpp").read_text()
pathlib.Path(sys.argv[2]).write_text(definition(source, "void EpubReaderActivity::loop()") + "\n")
