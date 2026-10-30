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
    if "void EpubReaderActivity::runLinkPersistence(" in reader:
        methods.append(definition(reader, "void EpubReaderActivity::runLinkPersistence("))
    if "bool EpubReaderActivity::runNavigationPolicy(" in reader:
        methods = [(repo / "test/reader_lifecycle/restore_navigation_rust.inc").read_text()
                   if method.startswith("void EpubReaderActivity::restoreSavedPosition(") else method
                   for method in methods]
    if "bool EpubReaderActivity::applyPageMove(" in reader:
        methods.append(definition(reader, "bool EpubReaderActivity::applyPageMove("))
    if "uint32_t EpubReaderActivity::runProjection(" in reader:
        # The complete projection binding is checked by test_projection.py; this
        # older lifecycle fixture keeps its narrower turn/gate platform surface.
        import re
        projection = definition(reader, "uint32_t EpubReaderActivity::runProjection(")
        read = projection.split("uint32_t read(", 1)[1].split("void write(", 1)[0]
        write = projection.split("void write(", 1)[1].split("uint32_t command(", 1)[0]
        command = projection.split("uint32_t command(", 1)[1].split("} context", 1)[0]
        def cases(text, kind, names):
            result = []
            for name in names:
                start = text.index("case XP_UI_READER_PROJECTION_" + kind + "_" + name + ":")
                end = re.search(r"\n\s*(?:case |default:)", text[start + 5:])
                assert end
                result.append(text[start:start + 5 + end.start()])
            return "\n".join(result)
        fields = ["HAS_SECTION", "SPINE", "PAGE", "PAGE_COUNT", "BUILDING"]
        writes = ["SPINE", "NEXT_PAGE", "PAGE", "PENDING_PAGE", "LAST_TURN"]
        commands = ["SPINE_COUNT", "LOCK", "UNLOCK", "CLEAR_DEFERRED", "ACKNOWLEDGE", "DROP_SECTION", "CLOCK"]
        bridge = """uint32_t EpubReaderActivity::runProjection(uint8_t operation,int32_t a,int32_t b,uint8_t c,void*) const {
          struct Context { EpubReaderActivity& o; std::optional<RenderLock> lock;
          uint32_t read(uint8_t f) { switch(f) { READ default:return 0; } }
          void write(uint8_t f,uint32_t value) { switch(f) { WRITE default:return; } }
          uint32_t command(uint8_t command,int32_t a,int32_t b) { switch(command) { COMMAND default:break; } return 0; }
          } context{const_cast<EpubReaderActivity&>(*this),{}};
        """
        bridge = bridge.replace("READ", cases(read,"FIELD",fields)).replace("WRITE", cases(write,"FIELD",writes)).replace("COMMAND", cases(command,"COMMAND",commands))
        bridge += projection[projection.index("  const XpUiReaderProjectionServices services"):]
        methods.append(bridge)
    if "bool EpubReaderActivity::runBuildPolicy(" in reader:
        methods.append((repo / "test/reader_lifecycle/build_policy_rust.inc").read_text())
    if "bool EpubReaderActivity::runBookLifetime(" in reader:
        methods.append((repo / "test/reader_lifecycle/load_progress_rust.inc").read_text())
    else:
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
