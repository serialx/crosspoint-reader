#pragma once
#include <xp_ui_catalog.h>

#include <numeric>
#include <string_view>
namespace plugincatalogpaging {
enum class Paging : uint8_t { Static, Offset, PageNumber, SizedPages };

template <typename Headers>
inline Paging pagingOf(std::string_view url, std::string_view body, const Headers& headers,
                       std::string_view origin = {}) {
  const auto variables = [](std::string_view text) { return xp_ui_catalog_page_template(text.data(), text.size()); };
  uint8_t flags = variables(url) | variables(body);
  if (!origin.empty()) flags |= variables(origin);
  flags = std::accumulate(headers.begin(), headers.end(), flags, [&](uint8_t found, const auto& header) {
    return static_cast<uint8_t>(found | variables(header.second));
  });
  if (flags & XP_UI_CATALOG_TEMPLATE_OFFSET) return Paging::Offset;
  if (!(flags & XP_UI_CATALOG_TEMPLATE_PAGE)) return Paging::Static;
  return flags & XP_UI_CATALOG_TEMPLATE_LIMIT ? Paging::SizedPages : Paging::PageNumber;
}

inline int limitFor(Paging paging, int pageSize) { return paging == Paging::SizedPages ? pageSize : pageSize + 1; }

// A static list is sliced after filtering; server pages count every returned row.
inline bool countsTowardPage(Paging paging, bool shown) { return paging != Paging::Static || shown; }

class PageWindow {
 public:
  PageWindow(size_t start, size_t limit) : window{start, limit, 0, 0} {}
  bool take(bool eligible = true) { return xp_ui_catalog_window_take(&window, eligible); }
  bool hasMore(Paging paging) const {
    return window.more ||
           (paging == Paging::SizedPages && window.seen >= window.start && window.seen - window.start >= window.limit);
  }

 private:
  XpUiCatalogWindow window;
};
static_assert(sizeof(XpUiCatalogWindow) == 4 * sizeof(size_t));
}  // namespace plugincatalogpaging
