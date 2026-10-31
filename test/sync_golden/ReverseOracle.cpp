#include <Epub/Section.h>
#include <ProgressMapper.h>

#include <iostream>

namespace {
void emit(const CrossPointPosition& p) {
  std::cout << p.spineIndex << ',' << p.pageNumber << ',' << p.totalPages << ',' << p.visibleTextOffset << ','
            << p.hasVisibleTextOffset << ',' << p.paragraphIndex << ',' << p.hasParagraphIndex << ',' << p.liIndex
            << ',' << p.hasLiIndex << ',' << p.hasResolvedSpineIndex << ',' << p.hasMappedPage << ':';
  for (unsigned char c : p.xpathAnchorId) std::cout << "0123456789abcdef"[c >> 4] << "0123456789abcdef"[c & 15];
  std::cout << '\n';
}
}  // namespace
int main() {
  const std::string xml =
      "<html><head>hidden</head><body>raw\r\n<p id='outer'>A&amp;B&#65;<a title=\"a/b\" id = "
      "'anchor'/>é中</p><ul><li>first</li><li>second</li></ul><script>skip</script><img/></body></html>";
  const std::vector<std::string> paths = {"/body/DocFragment[1]/body/p[1]/text()[2].1",
                                          "/body/DocFragment[1]/body/ul/li[2]/text().3",
                                          "/body/DocFragment[1]/body/img.0",
                                          "/body/DocFragment[1]/body/text().2",
                                          "/p[1]/text().2",
                                          "/body/DocFragment[1]/body/p[1].0",
                                          "/body/DocFragment[2]/body",
                                          "/body/DocFragment[999999999999999]/body/p[1].0"};
  GfxRenderer renderer;
  auto run = [&](const std::string& chapter, const std::string& path, size_t chunk, int cache) {
    auto epub = std::make_shared<Epub>(std::vector<std::string>{chapter, "<html><body><p>end</p></body></html>"});
    epub->testChunkSize = chunk;
    fake::cacheMode = cache;
    emit(ProgressMapper::toCrossPoint(epub, {path, 0.4f}, renderer, 0, 9, 3));
  };
  for (size_t i = 0; i <= xml.size(); ++i) {
    const std::string shortened = xml.substr(0, i);
    std::string flipped = xml;
    if (i < flipped.size()) flipped[i] ^= 0x20;
    for (const auto& path : paths)
      for (size_t chunk : {1u, 7u, 1024u}) {
        run(shortened, path, chunk, 0);
        run(flipped, path, chunk, 1);
      }
  }
  for (const auto& path : paths)
    for (size_t i = 0; i < path.size(); ++i) {
      auto erased = path;
      erased.erase(i, 1);
      auto flipped = path;
      flipped[i] ^= 0x40;
      for (int cache : {0, 1, 2, 3, 4}) {
        run(xml, erased, 7, cache);
        run(xml, flipped, 7, cache);
      }
    }
  fake::cacheMode = 5;
  auto epub = std::make_shared<Epub>(std::vector<std::string>{xml, xml});
  for (uint16_t spine : {0, 1}) {
    KOReaderRichPosition rich{
        400000, spine,        3,
        11,     std::nullopt, "/body/DocFragment[" + std::to_string(2 - spine) + "]/body/missing[1]/text().1"};
    auto position = ProgressMapper::fromRichPosition(epub, rich, renderer, false);
    std::cout << position.has_value() << ':';
    if (position) emit(*position);
  }
  // Unindexed and zero-index steps can match again after an earlier short element.
  for (const std::string& chapter : {"<html><body><p id='anchor'>a</p><p>abcd</p></body></html>",
                                     "<html><body><p id='anchor'>a</p><p id='x'>abcd</p></body></html>",
                                     "<html><body><p id='anchor'>a</p><p><a/>abcd</p></body></html>",
                                     "<html><body><div><p id='anchor'>a</p><p>abcd</p></div></body></html>"}) {
    for (const std::string& step : {"p", "p[0]", "div/p", "div[0]/p[0]"})
      for (size_t chunk : {1u, 7u, 1024u})
        for (int cache : {0, 1, 2, 3, 4}) run(chapter, "/body/DocFragment[1]/body/" + step + "/text().2", chunk, cache);
  }
}
