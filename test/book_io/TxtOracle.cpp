#include <Epub/BookMetadataCache.h>
#include <Txt.h>
#include <TxtToHtml.h>
#include <cstdio>
#include <memory>

size_t run(unsigned mode, size_t fail) {
  booktest::files.clear();
  booktest::files["/books/title.txt"] = {1, 2, 3};
  if (mode & 1) booktest::files["/cache"] = {};
  if (mode & 2) {
    const std::string html = std::string("<?xml version=\"1.0\"?>\n") +
        ((mode & 4) ? "invalid" : TxtToHtml::cacheVersionTag("title.txt"));
    booktest::files["/cache/html/0.html"] = {html.begin(), html.end()};
  }
  if (mode & 8) booktest::files["/books/COVER.PNG"] = {42};
  booktest::calls = 0; booktest::fail = fail;
  std::printf("case %u %zu\n", mode, fail);
  auto cache = std::make_unique<BookMetadataCache>("/cache");
  std::printf("valid %d\n", Txt::validateCache("/books/title.txt", "/cache", mode & 16 ? 4 : 3));
  std::printf("build %d\n", Txt::buildTxtCache("/books/title.txt", "/cache", cache));
  std::printf("cover %s\n", Txt::findCompanionCoverImage("/books/title.txt").c_str());
  cache.reset();
  return booktest::calls;
}
size_t cover(unsigned mode, size_t fail) {
  booktest::files.clear();
  const char* extension[] = {".bmp", ".jpg", ".png", ".no"};
  const std::string path = std::string("/cover") + extension[mode % 4];
  if (!(mode & 4)) booktest::files[path] = std::vector<uint8_t>(300, 42);
  booktest::calls = 0; booktest::fail = fail;
  std::printf("cover-case %u %zu\n", mode, fail);
  std::printf("convert %d\n", Txt::convertCoverImageToBmp(path, "/out", mode & 8 ? 50 : 0, mode & 16, mode & 32));
  for (const auto& [name, data] : booktest::files) {
    std::printf("file %s %zu ",name.c_str(),data.size());
    for (auto byte:data) std::printf("%02x",byte);
    std::puts("");
  }
  return booktest::calls;
}
int main() {
  for (unsigned mode = 0; mode < 64; ++mode) {
    const size_t steps = cover(mode, 0);
    for (size_t fail = 1; fail <= steps; ++fail) cover(mode, fail);
  }
  for (unsigned mode = 0; mode < 32; ++mode) {
    const size_t steps = run(mode, 0);
    for (size_t fail = 1; fail <= steps; ++fail) run(mode, fail);
  }
}
