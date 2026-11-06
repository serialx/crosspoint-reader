#pragma once

#include "src/BookmarkEntry.h"

// One Rust record vector; this historical row is only the JSON dump view.
class RustRecords {
 public:
  RustRecords();
  ~RustRecords();
  void push_back(const BookmarkEntry&);
  size_t size() const;
  const BookmarkEntry& operator[](size_t) const;
  bool load(const std::string&);
  bool save(const std::string&) const;
  static std::string path(const std::string&);

 private:
  void* owner;
  mutable BookmarkEntry snapshot{};
};
