#include <Epub.h>
#include <Epub/Page.h>
#include <Epub/Section.h>
#include <GfxRenderer.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace {
void state(Section& section, const char* label) {
  printf("state %s %u %d %d %d %u %u\n", label, section.pageCount, section.isBuilding(), section.isBuildComplete(),
         section.isPartial(), section.estimatedTotalPages(), section.estimatedTotalPages());
}
void queries(Section& section) {
  for (const int index : {-1, 0, 1, 3, 20}) {
    auto page = section.loadPage(index);
    printf("page %d %d\n", index, bool(page));
    printf("toc %d %d\n", index, section.getTocIndexForPage(index));
  }
  const auto out = [](const char* name, const auto& value) {
    printf("query %s %d %u\n", name, value.has_value(), value.value_or(0));
  };
  for (const char* anchor : {"start", "middle", "end", "absent"}) {
    printf("anchor %s\n", anchor);
    out("find", section.findAnchor(anchor));
    out("disk", section.getPageForAnchor(anchor));
    out("build", section.findAnchorDuringBuild(anchor));
  }
  out("cached", section.getCachedPageCount());
  for (uint16_t index : {0, 1, 5, 65535}) {
    printf("index %u\n", index);
    out("paragraph", section.getParagraphIndexForPage(index));
    out("paragraph-page", section.getPageForParagraphIndex(index));
    out("list-page", section.getPageForListItemIndex(index));
    out("visible", section.getVisibleTextOffsetForPage(index));
    out("offset-last", section.getPageForVisibleTextOffset(index));
    out("offset-first", section.getPageForVisibleTextOffset(index, true));
    printf("reached %d\n", section.buildReachedVisibleTextOffset(index));
  }
  section.currentPage = 0;
  const auto text = section.getTextFromSectionFile();
  printf("text ");
  for (unsigned char c : text) printf("%02x", c);
  puts("");
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) return 2;
  BookFault::failAt = std::strtoul(argv[2], nullptr, 10);
  auto epub = std::make_shared<Epub>();
  epub->spine = {{"chapter.xhtml"}};
  epub->toc = {{0, ""}, {0, "start"}, {0, "middle"}, {0, "end"}, {1, "absent"}};
  GfxRenderer renderer;
  ReaderRenderSpec spec;
  spec.fontId = 16;
  spec.viewportWidth = 180;
  spec.viewportHeight = 220;
  spec.embeddedStyle = false;
  const std::string mode = argv[1];
  {
    Section section(epub, 0, renderer);
    printf("html %d\n", section.hasHtmlCache());
    printf("load %d\n", section.loadSectionFile(spec));
    if (mode == "full") {
      printf("create %d\n", section.createSectionFile(spec));
    } else {
      printf("start %d\n", section.startBuild(spec));
      printf("start-again %d\n", section.startBuild(spec));
      printf("advance %d\n", section.buildSomeMore(1));
      state(section, "advanced");
      queries(section);
      if (mode == "abandon")
        section.abandonBuild();
      else
        section.suspendBuild();
    }
    state(section, "finished");
    queries(section);
  }
  {
    Section section(epub, 0, renderer);
    printf("reload %d\n", section.loadSectionFile(spec));
    state(section, "reloaded");
    if (mode == "resume") {
      printf("restart %d\n", section.startBuild(spec));
      printf("yield %d\n", section.buildSomeMore(0, [] { return true; }));
      state(section, "yielded");
      printf("finish %d\n", section.buildSomeMore(0));
    }
    queries(section);
    state(section, "done");
  }
  printf("steps %zu\n", BookFault::step);
}
