#pragma once
#include <string_view>
namespace FsHelpers {
inline bool hasPngExtension(std::string_view s) { return s.ends_with(".png"); }
inline bool hasJpgExtension(std::string_view s) { return s.ends_with(".jpg") || s.ends_with(".jpeg"); }
inline bool hasGifExtension(std::string_view s) { return s.ends_with(".gif"); }
}  // namespace FsHelpers
