#include <Epub.h>
#include <Epub/Section.h>
#include <Epub/SectionCodec.h>
#include <GfxRenderer.h>

#include <cstdio>
#include <cstring>
#include <filesystem>

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  std::filesystem::create_directories("cache/sections");
  auto header = sectioncodec::header(ReaderRenderSpec{}, 1);
  header.values[0] = std::strcmp(argv[1], "version") == 0 ? 255 : XP_SEC_VERSION;
  header.values[19] = XP_SEC_HEADER_SIZE;
  HalFile f;
  if (!f.open("cache/sections/0.bin", "wb") || !sectioncodec::write(f, header) ||
      !sectioncodec::writeValue(f, XP_SEC_U32, 10))
    return 3;
  f.close();
  auto epub = std::make_shared<Epub>();
  GfxRenderer renderer;
  Section section(epub, 0, renderer);
  ReaderRenderSpec spec{};
  if (std::strcmp(argv[1], "parameters") == 0) spec.fontId += 1;
  if (std::strcmp(argv[1], "seek") == 0 || std::strcmp(argv[1], "read") == 0) {
    if (std::strcmp(argv[1], "seek") == 0)
      HalFileTest::failSeekAt = XP_SEC_HEADER_SIZE;
    else
      HalFileTest::failReadAt = XP_SEC_HEADER_SIZE;
    std::printf("RESULT %d\n", section.getPageForVisibleTextOffset(10).has_value());
  } else {
    std::printf("RESULT %d\n", section.loadSectionFile(spec));
  }
}
