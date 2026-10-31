#pragma once

#include <string>
#include <string_view>

namespace FsHelpers {

inline std::string normalisePath(std::string_view path) { return std::string(path); }
inline std::string decodeUriEscapes(const std::string& path) { return path; }

}  // namespace FsHelpers
