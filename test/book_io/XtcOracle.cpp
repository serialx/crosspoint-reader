#include <HalStorage.h>
#include <Xtc.h>
#include <fstream>
#include <iterator>

void run(const std::vector<uint8_t>& bytes, size_t fail, bool images) {
  booktest::files.clear();
  booktest::files["book.xtc"] = bytes;
  booktest::calls = 0;
  booktest::fail = fail;
  std::printf("case %zu %zu %d\n", bytes.size(), fail, images);
  if (images) {
    Xtc book("book.xtc", "/cache");
    std::printf("load %d\n", book.load());
    std::printf("title %s\n", book.getTitle().c_str());
    std::printf("cover %d\n", book.generateCoverBmp());
    std::printf("thumb %d\n", book.generateThumbBmp(3));
    std::printf("progress %u\n", book.calculateProgress(1));
  } else {
    xtc::XtcParser parser;
    std::printf("open %d\n", static_cast<int>(parser.open("book.xtc")));
    std::printf("metadata %s %s %u %u %u\n", parser.getTitle().c_str(), parser.getAuthor().c_str(),
                parser.getPageCount(), parser.getWidth(), parser.getHeight());
    for (const auto& chapter : parser.getChapters()) {
      std::printf("chapter %s %u %u\n", chapter.name.c_str(), chapter.startPage, chapter.endPage);
    }
    for (uint32_t index : {0u, 1u, 3u, UINT32_MAX}) {
      uint8_t pixels[256] = {};
      size_t count = parser.loadPage(index, pixels, sizeof(pixels));
      std::printf("page %u %zu %d\n", index, count, static_cast<int>(parser.getLastError()));
      for (auto byte : pixels) std::printf("%02x", byte);
      std::puts("");
    }
  }
  for (const auto& [name, content] : booktest::files) {
    if (name == "book.xtc") continue;
    std::printf("file %s\n", booktest::displayPath(name).c_str());
    for (auto byte : content) std::printf("%02x", byte);
    std::puts("");
  }
}
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  std::ifstream input(argv[1], std::ios::binary);
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
  for (bool images : {false, true}) {
    run(bytes, 0, images);
    const size_t steps = booktest::calls;
    for (size_t fail = 1; fail <= steps; ++fail) run(bytes, fail, images);
  }
}
