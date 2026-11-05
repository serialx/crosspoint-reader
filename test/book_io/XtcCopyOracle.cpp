#include <HalStorage.h>
#include <Xtc.h>

#include <fstream>
#include <iterator>

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  std::ifstream input(argv[1], std::ios::binary);
  const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
  for (bool cachedCover : {false, true}) {
    booktest::files.clear();
    booktest::files["book.xtc"] = bytes;
    booktest::calls = booktest::fail = 0;
    Xtc book("book.xtc", "/cache");
    printf("copy case cached=%u\n", cachedCover);
    if (!book.load()) return 3;
    if (cachedCover && !book.generateCoverBmp()) return 4;
    booktest::calls = 0;
    if (!book.generateThumbBmp(300)) return 5;
    const auto cover = booktest::files.find(book.getCoverBmpPath());
    const auto thumb = booktest::files.find(book.getThumbBmpPath(300));
    if (cover == booktest::files.end() || thumb == booktest::files.end()) return 6;
    if (cover->second.empty() || cover->second != thumb->second) return 7;
    printf("copy equal bytes=%zu calls=%zu\n", thumb->second.size(), booktest::calls);
    for (const auto& [name, content] : booktest::files) {
      if (name == "book.xtc") continue;
      printf("file %s\n", booktest::displayPath(name).c_str());
      for (auto byte : content) printf("%02x", byte);
      puts("");
    }
  }
}
