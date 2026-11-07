"""Put shipping store adapters between the real Rust owner and physical stores."""
from test_popup_resource import definition, native_leaf


def store_adapters(root):
    native = (root / 'src/activities/reader/RustEpubResources.cpp').read_text()
    source = r'''
#include <string_view>
namespace BookStoreAdapters {
static void (*recents)(const XpAppBook*);
XpAppText wire(std::string_view value) {
  return {reinterpret_cast<const uint8_t*>(value.data()), value.size()};
}
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
               'recents = leaves.recents_add;\n')
    for field in ('recents_add',):
        source += f'leaves.{field} = {native_leaf(native, field)};\n'
    return source + '}\n} // namespace BookStoreAdapters\n'
