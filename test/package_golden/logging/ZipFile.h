#pragma once
#define ZipFile PackageOracleZipFile
#include "../../book_metadata_cache/stubs/ZipFile.h"
#undef ZipFile

class ZipFile : public PackageOracleZipFile {
 public:
  using PackageOracleZipFile::PackageOracleZipFile;
  static inline bool failOpen = false;
  bool open() { return !failOpen; }
};
