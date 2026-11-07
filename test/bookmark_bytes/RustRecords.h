#pragma once

#include "BookmarkOracleFixture.h"

// One Rust record vector; this historical row is only the JSON dump view.
class RustRecords {
 public:
  RustRecords();
  ~RustRecords();
  void push_back(const BookmarkOracleEntry&);
  size_t size() const;
  const BookmarkOracleEntry& operator[](size_t) const;
  bool load(const std::string&);
  bool save(const std::string&) const;
  static std::string path(const std::string&);

 private:
  void* owner;
  mutable BookmarkOracleEntry snapshot{};
};
