#include <HalStorage.h>
#include <Logging.h>
#include <Xtc/RustXtcParser.h>
#include <Xtc/XtcParser.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>

static void check(bool yes) {
  if (!yes) std::abort();
}
template <class Parser>
static void run() {
  Parser parser;
  std::printf("probe %d\n", parser.isValidXtcFile("book.xtc"));
  const auto result = parser.open("book.xtc");
  std::printf("open %d last %d size %u %u depth %u pages %u\n", int(result), int(parser.getLastError()),
              parser.getWidth(), parser.getHeight(), parser.getBitDepth(), parser.getPageCount());
  const auto& header = parser.getHeader();
  const auto* bytes = reinterpret_cast<const uint8_t*>(&header);
  for (size_t i = 0; i < sizeof(header); ++i) std::printf("%02x", bytes[i]);
  std::printf("\n");
  if (result == decltype(result)::OK) {
    const std::string_view title = parser.getTitle(), author = parser.getAuthor();
    std::printf("title %zu ", title.size());
    for (auto ch : title) std::printf("%02x", uint8_t(ch));
    std::printf(" author %zu ", author.size());
    for (auto ch : author) std::printf("%02x", uint8_t(ch));
    std::printf(" flag %d\n", parser.hasChapters());
    const auto& rows = parser.getChapters();
    std::printf("chapters %zu flag %d\n", rows.size(), parser.hasChapters());
    for (const auto& row : rows) {
      if constexpr (std::is_same_v<Parser, xtc::RustXtcParser>) {
        std::printf("row %u %u %zu ", row.start_page, row.end_page, row.name_len);
        for (size_t i = 0; i < row.name_len; ++i) std::printf("%02x", row.name[i]);
        check(row.name[row.name_len] == 0);
      } else {
        std::printf("row %u %u %zu ", row.startPage, row.endPage, row.name.size());
        for (auto ch : row.name) std::printf("%02x", uint8_t(ch));
      }
      std::printf("\n");
    }
    for (uint32_t index : {0u, 1u, 2u, 3u, UINT32_MAX}) {
      xtc::PageInfo info{};
      std::printf("info %u %d ", index, parser.getPageInfo(index, info));
      std::printf("%llu %u %u %u %u\n", static_cast<unsigned long long>(info.offset), info.size, info.width,
                  info.height, info.bitDepth);
      uint8_t page[4096];
      std::fill(std::begin(page), std::end(page), 0xee);
      const auto size = parser.loadPage(index, page, sizeof(page));
      std::printf("load %zu error %d ", size, int(parser.getLastError()));
      for (auto b : page) std::printf("%02x", b);
      std::printf("\n");
      const auto result = parser.loadPageStreaming(
          index,
          [](const uint8_t* data, size_t n, size_t offset) {
            std::printf("emit %zu %zu ", offset, n);
            for (size_t i = 0; i < n; ++i) std::printf("%02x", data[i]);
            std::printf("\n");
          },
          17);
      std::printf("stream %d\n", int(result));
    }
  }
  parser.close();
  std::printf("closed %d\n", parser.isOpen());
  const auto again = parser.open("book.xtc");
  std::printf("reopen %d\n", int(again));
}
int main(int argc, char** argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--layout") {
    std::printf(
        "native_parser=%zu rust_parser=%zu file=%zu native_string=%zu small_capacity=%zu native_row=%zu rust_row=%zu "
        "owner=%zu\n",
        sizeof(xtc::XtcParser), sizeof(xtc::RustXtcParser), sizeof(HalFile), sizeof(std::string),
        std::string().capacity(), sizeof(xtc::ChapterInfo), sizeof(XpXtcChapterRow), sizeof(XpXtcOwnerStorage));
    return 0;
  }
  check(argc == 4);
  std::ifstream file(argv[1], std::ios::binary);
  booktest::files["book.xtc"] = {std::istreambuf_iterator<char>(file), {}};
  booktest::fail = std::stoul(argv[3]);
  if (std::string_view(argv[2]) == "base")
    std::abort();
  else
    run<xtc::RustXtcParser>();
  for (const auto& line : xtcowner::logs) std::printf("log %s\n", line.c_str());
  std::printf("calls %zu\n", booktest::calls);
}
