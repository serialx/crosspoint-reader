#include "LibraryText.h"

#include <xp_ffi.h>
namespace library {
namespace {
std::string transform(std::string_view text, uint8_t operation) {
  std::string result(text);
  result.resize(xp_library_text(reinterpret_cast<uint8_t*>(result.data()), result.size(), operation));
  return result;
}
}  // namespace
std::string fold(std::string_view s) { return transform(s, 0); }
std::string cleanPersonName(std::string_view s) { return transform(s, 1); }
std::string authorKey(std::string_view s) { return transform(s, 2); }
std::string surnameKey(std::string_view s) { return transform(s, 3); }
uint32_t foldedGroupInitial(std::string_view s) {
  return xp_library_initial(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}
bool matchesQuery(std::string_view a, std::string_view b) {
  return xp_library_matches(reinterpret_cast<const uint8_t*>(a.data()), a.size(),
                            reinterpret_cast<const uint8_t*>(b.data()), b.size());
}
std::string joinLibraryPath(std::string_view folder, std::string_view name) {
  std::string result(folder.size() + name.size() + 1, '\0');
  result.resize(xp_library_join(reinterpret_cast<const uint8_t*>(folder.data()), folder.size(),
                                reinterpret_cast<const uint8_t*>(name.data()), name.size(),
                                reinterpret_cast<uint8_t*>(result.data())));
  return result;
}
}  // namespace library
