#pragma once
#include <xp_ui_catalog.h>

#include <string_view>
namespace plugincatalogpaging {
template <typename Headers>
inline bool isStaticFeed(std::string_view url, std::string_view body, const Headers& headers) {
  if (xp_ui_catalog_page_template(url.data(), url.size()) || xp_ui_catalog_page_template(body.data(), body.size()))
    return false;
  for (const auto& header : headers)
    if (xp_ui_catalog_page_template(header.second.data(), header.second.size())) return false;
  return true;
}
class PageWindow {
 public:
  PageWindow(size_t start, size_t limit) : window{start, limit, 0, 0} {}
  bool take(bool eligible = true) { return xp_ui_catalog_window_take(&window, eligible); }
  bool hasMore() const { return window.more; }

 private:
  XpUiCatalogWindow window;
};
static_assert(sizeof(XpUiCatalogWindow) == 4 * sizeof(size_t));
}  // namespace plugincatalogpaging
