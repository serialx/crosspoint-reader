"""Put shipping store adapters between the real Rust owner and physical stores."""
from test_popup_resource import definition, native_leaf


def store_adapters(root):
    native = (root / 'src/activities/reader/RustEpubResources.cpp').read_text()
    source = r'''
#include <string_view>
namespace BookStoreAdapters {
static uint8_t (*load)(void*, XpAppText);
static uint8_t (*save)(void*, XpAppText);
static uint8_t (*clippings)(const XpAppBook*);
static void (*recents)(const XpAppBook*);
XpAppText wire(std::string_view value) {
  return {reinterpret_cast<const uint8_t*>(value.data()), value.size()};
}
// The unchanged stores are physical boundaries; keep the existing observations.
void* bookmarkRows(void* raw) { return raw; }
struct BookmarkFile {
  static uint8_t load(const std::string& path, void* rows) {
    return BookStoreAdapters::load(rows, wire(path));
  }
  static uint8_t save(const std::string& path, void* rows) {
    return BookStoreAdapters::save(rows, wire(path));
  }
};
struct Clippings {
  uint8_t loadForBook(std::string_view path, std::string_view title,
                     std::string_view author, std::string_view format) {
    assert(std::string(format) == "epub");
    XpAppBook row{}; row.path = wire(path); row.title = wire(title); row.author = wire(author);
    return clippings(&row);
  }
} CLIPPINGS;
struct Recents {
  void addBook(std::string_view path, std::string_view title,
               std::string_view author, std::string_view cover) {
    XpAppBook row{}; row.path = wire(path); row.title = wire(title);
    row.author = wire(author); row.cover = wire(cover); recents(&row);
  }
} RECENT_BOOKS;
'''
    source += definition(native, 'std::string owned(XpAppText value)') + '\n'
    source += definition(native, 'std::string_view view(XpAppText value)') + '\n'
    source += ('void bind() {\n'
               'load = leaves.bookmarks_load; save = leaves.bookmarks_save;\n'
               'clippings = leaves.clippings_load; recents = leaves.recents_add;\n')
    for field in ('bookmarks_load', 'bookmarks_save', 'clippings_load', 'recents_add'):
        source += f'leaves.{field} = {native_leaf(native, field)};\n'
    return source + '}\n} // namespace BookStoreAdapters\n'
