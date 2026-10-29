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
int main() {
  for (unsigned mode = 0; mode < 32; ++mode) {
    const size_t steps = run(mode, 0);
    for (size_t fail = 1; fail <= steps; ++fail) run(mode, fail);
  }
}
