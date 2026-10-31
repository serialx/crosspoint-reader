#include <Epub.h>
#include <Epub/Page.h>
#include <Epub/Section.h>
#include <GfxRenderer.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#include "../section_io/SectionTestCodec.h"

namespace {
void require(bool ok, const char* message) {
  if (!ok) {
    fprintf(stderr, "FAILED: %s\n", message);
    std::exit(1);
  }
}
ReaderRenderSpec spec() {
  ReaderRenderSpec value;
  value.fontId = 16;
  value.viewportWidth = 180;
  value.viewportHeight = 220;
  value.embeddedStyle = false;
  return value;
}
std::vector<uint8_t> contents(const char* path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), {}};
}
void html(size_t paragraphs) {
  std::filesystem::create_directories("book");
  std::ofstream file("book/chapter.xhtml");
  file << "<html><body>";
  for (size_t i = 0; i < paragraphs; ++i)
    file << "<p id='p" << i << "'>Alpha beta gamma delta epsilon zeta eta theta.</p>";
  file << "<a id='eof'></a></body></html>";
}
void reset() { std::filesystem::remove_all("cache"); }
void snapshot(Section& section, const char* label) {
  const auto estimate = section.estimatedTotalPages();
  require(estimate == section.estimatedTotalPages(), "estimate advances without parser progress");
  require(estimate >= section.pageCount, "estimate below readable pages");
  printf("state %s %u %u %d %d %d\n", label, section.pageCount, estimate, section.isBuilding(),
         section.isBuildComplete(), section.isPartial());
}
void inspect(Section& section) {
  for (int index : {-1, 0, static_cast<int>(section.pageCount) - 1, static_cast<int>(section.pageCount)}) {
    const auto page = section.loadPage(index);
    printf("read %d %d\n", index, bool(page));
  }
  for (const char* key : {"p0", "p10", "eof", "missing"}) {
    const auto page = section.findAnchor(key);
    printf("anchor %s %d %u\n", key, page.has_value(), page.value_or(0));
  }
  printf("toc %d\n", section.getTocIndexForPage(section.pageCount - 1));
}
void lut(const std::shared_ptr<Epub>& epub, GfxRenderer& renderer) {
  for (uint16_t count : {0, 1, 400}) {
    reset();
    std::filesystem::create_directories("cache/sections");
    HalFile file;
    require(file.open("cache/sections/0.bin", "wb"), "fixture open");
    auto header = sectioncodec::header(spec(), count);
    header.values[0] = XP_SEC_VERSION;
    header.values[15] = XP_SEC_HEADER_SIZE + count * 6;
    header.values[16] = header.values[15] + count * 4;
    header.values[17] = header.values[16] + 2;
    header.values[18] = header.values[17] + 2 + count * 2;
    header.values[19] = header.values[18] + count * 2;
    require(sectioncodec::write(file, header), "header");
    const uint8_t emptyPage[6]{};
    for (size_t i = 0; i < count; ++i) require(file.write(emptyPage, 6) == 6, "page");
    for (size_t i = 0; i < count; ++i)
      require(sectioncodec::writeValue(file, XP_SEC_U32, XP_SEC_HEADER_SIZE + i * 6), "page LUT");
    require(sectioncodec::writeValue(file, XP_SEC_U16, 0), "empty anchors");
    require(sectioncodec::writeValue(file, XP_SEC_U16, count), "paragraph count");
    for (size_t i = 0; i < count * 2; ++i)
      require(sectioncodec::writeValue(file, XP_SEC_U16, i % count), "paragraph/list LUT");
    for (size_t i = 0; i < count; ++i)
      require(sectioncodec::writeValue(file, XP_SEC_U32, i * 10000000u), "visible LUT");
    file.close();
    Section section(epub, 0, renderer);
    require(section.loadSectionFile(spec()), "load empty/one/large LUT");
    require(section.pageCount == count, "LUT count");
    require(!section.loadPage(count), "page after LUT");
    for (uint16_t i = 0; i < count; ++i) {
      require(bool(section.loadPage(i)), "LUT page");
      require(section.getParagraphIndexForPage(i) == i, "paragraph field");
      require(section.getVisibleTextOffsetForPage(i) == i * 10000000u, "full-width visible field");
    }
    snapshot(section, "lut");
  }
}
void lifecycle(const std::shared_ptr<Epub>& epub, GfxRenderer& renderer, bool interleaved) {
  html(900);
  std::vector<uint8_t> uninterrupted;
  {
    Section section(epub, 0, renderer);
    require(section.createSectionFile(spec()), "one-shot build");
    require(section.pageCount > 100, "fixture must grow LUT nodes");
    uninterrupted = contents("cache/sections/0.bin");
  }
  reset();
  {
    Section section(epub, 0, renderer);
    require(section.startBuild(spec()), "start");
    require(!section.startBuild(spec()), "duplicate start");
    snapshot(section, "start");
    while (!section.isBuildComplete()) {
      if (interleaved) inspect(section);
      require(section.buildSomeMore(0, [] { return true; }), "yield step");
      snapshot(section, "advance");
      if (interleaved) inspect(section);
    }
    require(contents("cache/sections/0.bin") == uninterrupted, "reads disturbed writer or yield changed bytes");
    const auto eof = section.findAnchor("eof");
    printf("eof %d %u %u\n", eof.has_value(), eof.value_or(0), section.pageCount);
  }
  for (int advance : {1, 50}) {
    reset();
    {
      Section partial(epub, 0, renderer);
      require(partial.startBuild(spec()) && partial.buildSomeMore(25), "partial setup");
      partial.suspendBuild();
      require(partial.isPartial() && !partial.isBuilding(), "suspended state");
      snapshot(partial, "suspended");
    }
    const auto before = contents("cache/sections/0.bin");
    Section resumed(epub, 0, renderer);
    require(resumed.loadSectionFile(spec()), "partial reopen");
    const auto oldCount = resumed.pageCount;
    require(resumed.startBuild(spec()) && resumed.buildSomeMore(advance), "partial rebuild");
    inspect(resumed);
    snapshot(resumed, "rebuilding");
    resumed.suspendBuild();
    snapshot(resumed, "resuspended");
    if (advance == 1) {
      require(resumed.pageCount == oldCount, "less-complete rebuild replaced partial count");
      require(contents("cache/sections/0.bin") == before, "less-complete rebuild replaced partial bytes");
    } else {
      require(resumed.pageCount > oldCount, "more-complete rebuild did not replace partial");
    }
    require(resumed.startBuild(spec()), "restart before abandon");
    resumed.abandonBuild();
    require(!resumed.isBuilding() && !resumed.isPartial() && resumed.pageCount == 0, "abandon state");
  }
}
size_t writes = 0, shortAt = 0;
size_t shortWrite(const char*, size_t, size_t length) {
  ++writes;
  return writes == shortAt && length ? length - 1 : length;
}
void shortWrites(const std::shared_ptr<Epub>& epub, GfxRenderer& renderer, size_t target) {
  html(12);
  shortAt = target;
  HalFileTest::writeLimit = shortWrite;
  {
    Section section(epub, 0, renderer);
    printf("create %d\n", section.createSectionFile(spec()));
    snapshot(section, "short-write");
  }
  HalFileTest::writeLimit = nullptr;
  printf("writes %zu\n", writes);
  std::vector<std::filesystem::path> paths;
  for (const auto& entry : std::filesystem::recursive_directory_iterator("cache"))
    if (entry.is_regular_file()) paths.push_back(entry.path());
  std::sort(paths.begin(), paths.end());
  for (const auto& path : paths) {
    printf("file %s ", path.c_str());
    for (const auto byte : contents(path.c_str())) printf("%02x", byte);
    puts("");
  }
}
}  // namespace

int main(int argc, char** argv) {
  require(argc >= 2, "mode required");
  char directory[] = "/tmp/section-behavior-XXXXXX";
  require(mkdtemp(directory), "temporary directory");
  const auto original = std::filesystem::current_path();
  std::filesystem::current_path(directory);
  auto epub = std::make_shared<Epub>();
  epub->spine = {{"chapter.xhtml"}};
  epub->toc = {{0, ""}, {0, "p0"}, {0, "p10"}, {0, "eof"}};
  GfxRenderer renderer;
  const std::string mode = argv[1];
  if (mode == "lut")
    lut(epub, renderer);
  else if (mode == "lifecycle" || mode == "interleaving")
    lifecycle(epub, renderer, mode == "interleaving");
  else if (mode == "short")
    shortWrites(epub, renderer, argc == 3 ? std::strtoull(argv[2], nullptr, 10) : 0);
  else
    require(false, "unknown mode");
  std::filesystem::current_path(original);
  std::filesystem::remove_all(directory);
}
