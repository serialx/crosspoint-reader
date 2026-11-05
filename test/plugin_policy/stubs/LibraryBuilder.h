#pragma once
#include "Oracle.h"
namespace library {
inline bool isLibraryBookFile(const std::string& s) { return s.ends_with(".epub"); }
inline void markLibraryIndexDirty() { oracle::record("library-dirty"); }
}  // namespace library
