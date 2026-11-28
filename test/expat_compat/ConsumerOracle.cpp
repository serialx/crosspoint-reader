#include <OpdsFeedParser.h>
#include <OpenSearchDescParser.h>
#include <XmlListParser.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>

namespace {
void value(const std::string& s) {
  static constexpr char digits[] = "0123456789abcdef";
  for (const unsigned char c : s) std::cout << digits[c >> 4] << digits[c & 15];
  std::cout << '\n';
}
void entries(const std::vector<OpdsEntry>& rows) {
  std::cout << rows.size() << '\n';
  for (const auto& row : rows) {
    std::cout << static_cast<int>(row.type) << ' ' << row.purchase << ' ' << row.indirect << ' ' << row.lcpLicense
              << '\n';
    for (const auto* s : {&row.title, &row.author, &row.href, &row.id, &row.heading, &row.detail, &row.selfHref,
                          &row.description, &row.coverHref})
      value(*s);
  }
}
void feed(Print& parser, const std::string& text, size_t chunk) {
  for (size_t offset = 0; offset < text.size(); offset += chunk) {
    const size_t n = std::min(chunk, text.size() - offset);
    if (parser.write(reinterpret_cast<const uint8_t*>(text.data() + offset), n) != n) std::exit(3);
  }
  parser.flush();
}
}  // namespace
int main(int argc, char** argv) {
  if (argc != 4) return 2;
  std::ifstream input(argv[2], std::ios::binary);
  if (!input) return 2;
  const std::string text{std::istreambuf_iterator<char>(input), {}};
  const size_t chunk = std::stoul(argv[3]);
  if (!chunk) return 2;
  const std::string mode = argv[1];
  if (mode == "api") {
    auto parser = XML_ParserCreate(nullptr);
    if (!parser) return 3;
    auto status = XML_STATUS_OK;
    for (size_t offset = 0; offset < text.size() && status == XML_STATUS_OK; offset += chunk) {
      const size_t n = std::min(chunk, text.size() - offset);
      status = XML_Parse(parser, text.data() + offset, static_cast<int>(n), XML_FALSE);
    }
    if (status == XML_STATUS_OK) status = XML_Parse(parser, nullptr, 0, XML_TRUE);
    const auto error = XML_GetErrorCode(parser);
    std::cout << status << ' ' << error << '\n';
    if (error != XML_ERROR_NONE) {
      std::cout << XML_GetCurrentLineNumber(parser) << '\n';
      value(XML_ErrorString(error));
    }
    XML_ParserFree(parser);
  } else if (mode == "feed") {
    OpdsFeedParser parser;
    feed(parser, text, chunk);
    std::cout << parser.error() << ' ' << parser.truncated() << ' ' << parser.isOpds2() << ' ' << parser.currentPage()
              << ' ' << parser.pageCount() << '\n';
    for (const auto* s :
         {&parser.getFeedTitle(), &parser.getSearchTemplate(), &parser.getSearchDescriptionUrl(),
          &parser.getNextPageUrl(), &parser.getPrevPageUrl(), &parser.getFirstPageUrl(), &parser.getLastPageUrl(),
          &parser.getShelfUrl(), &parser.getWishlistUrl(), &parser.getHistoryUrl()})
      value(*s);
    entries(parser.takeEntries());
    entries(parser.takeFacetEntries());
  } else if (mode == "search") {
    OpenSearchDescParser parser;
    feed(parser, text, chunk);
    std::cout << parser.error() << '\n';
    value(parser.getTemplate());
  } else if (mode == "catalog") {
    const std::string selectors[] = {"href", "displayname", "author", "@id"};
    const std::string* const selection[] = {&selectors[0], &selectors[1], &selectors[2], &selectors[3]};
    std::vector<XmlListParser::RawItem> rows;
    auto parser = std::make_unique<XmlListParser>(
        "response", "collection", selection,
        [](void* ctx, XmlListParser::RawItem& row) { static_cast<decltype(rows)*>(ctx)->push_back(row); }, &rows);
    XmlListParser::UrlOptions urls;
    urls.requestUrl = "https://example.org/books/";
    urls.skipSelf = true;
    urls.resolveUrls = true;
    urls.extensions = {".epub"};
    parser->setUrlOptions(std::move(urls));
    struct Source {
      const std::string& text;
      size_t offset;
      size_t chunk;
    } source{text, 0, chunk};
    const bool ok = parser->parse(
        [](void* ctx, char* out, size_t capacity) {
          auto& s = *static_cast<Source*>(ctx);
          const size_t n = std::min({capacity, s.chunk, s.text.size() - s.offset});
          memcpy(out, s.text.data() + s.offset, n);
          s.offset += n;
          return static_cast<int>(n);
        },
        &source);
    std::cout << ok << ' ' << rows.size() << '\n';
    for (const auto& row : rows) {
      std::cout << row.isDir << '\n';
      for (const auto& s : row.field) value(s);
    }
  } else
    return 2;
}
