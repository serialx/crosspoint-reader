"""Compile production reader methods against deterministic platform leaves."""
import pathlib
import sys


def definition(source, signature):
    start = source.index(signature)
    body = source.index("{", start)
    depth = 1
    end = body + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def generate(repo, output):
    reader = (repo / "src/activities/reader/EpubReaderActivity.cpp").read_text()
    utils = (repo / "src/activities/reader/ReaderUtils.h").read_text()
    methods = [definition(reader, signature) for signature in (
        "bool EpubReaderActivity::pageTurn(",
        "bool EpubReaderActivity::skipPages(",
        "bool EpubReaderActivity::buildTickHeapGate(",
        "void EpubReaderActivity::backgroundBuildTick(",
        "bool EpubReaderActivity::backgroundBuildWanted(",
        "bool EpubReaderActivity::skipLoopDelay(",
        "void EpubReaderActivity::saveLinkStack(",
        "void EpubReaderActivity::loadLinkStack(",
        "void EpubReaderActivity::restoreSavedPosition(",
    )]
    if "bool EpubReaderActivity::applyPageMove(" in reader:
        methods.append(definition(reader, "bool EpubReaderActivity::applyPageMove("))
    load = definition(reader, 'if (Storage.openFileForRead("ERS", epub->getCachePath() + "/progress.bin", f))')
    methods.append("void EpubReaderActivity::loadProgress() { HalFile f; " + load + " }")
    loop = definition(reader, "void EpubReaderActivity::loop()")
    if "xp_ui_reader_loop_run" in loop:
        methods.append((repo / "test/reader_lifecycle/turn_input_rust.inc").read_text())
    else:
        suffix = loop[loop.index("  constexpr unsigned long kMinManualTurnGapMs"):]
        methods.append("void EpubReaderActivity::turnInput() { const auto touch = touchInput;\n" + suffix)
    methods.append("namespace ReaderUtils {\n" + "\n".join(
        definition(utils, signature) for signature in (
            "inline bool gestureAllowsSwipe(", "inline bool gestureAllowsTap(",
            "inline bool isRtlBookLanguage(", "inline void displayWithRefreshCycle(",
            "inline void displayBaseWithRefreshCycle(")) + "\n}")
    output.write_text("// Extracted from the current production sources; do not edit.\n" + "\n\n".join(methods) + "\n")


if __name__ == "__main__":
    generate(pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]))
