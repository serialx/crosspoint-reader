#pragma once
#include <Arduino.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <vector>
namespace store_test {
inline std::map<std::string, String> files;
inline std::vector<std::string> io;
inline bool failWrite = false;
inline bool failRead = false;
}  // namespace store_test
class HalFile {
 public:
  std::vector<uint8_t> bytes;
  size_t pos = 0;
  int readLimit = -1;
  size_t writeLimit = SIZE_MAX;
  bool failSeek = false;
  size_t position() const { return pos; }
  int available() const { return static_cast<int>(bytes.size() - std::min(pos, bytes.size())); }
  int read(void* p, size_t n) {
    store_test::io.push_back("read:" + std::to_string(n));
    if (readLimit == 0) return -1;
    if (readLimit > 0) n = std::min(n, static_cast<size_t>(readLimit));
    n = std::min(n, bytes.size() - std::min(pos, bytes.size()));
    if (n) memcpy(p, bytes.data() + pos, n);
    pos += n;
    return static_cast<int>(n);
  }
  size_t write(const void* p, size_t n) {
    store_test::io.push_back("write:" + std::to_string(n));
    n = std::min(n, writeLimit);
    bytes.resize(std::max(bytes.size(), pos + n));
    if (n) memcpy(bytes.data() + pos, p, n);
    pos += n;
    return n;
  }
  bool seek(size_t n) {
    store_test::io.push_back("seek:" + std::to_string(n));
    if (failSeek) return false;
    pos = n;
    return true;
  }
};
struct TestStorage {
  bool mkdir(const char* p) {
    store_test::io.push_back(std::string("mkdir:") + p);
    return true;
  }
  bool exists(const char* p) {
    store_test::io.push_back(std::string("exists:") + p);
    return store_test::files.count(p);
  }
  String readFile(const char* p) {
    store_test::io.push_back(std::string("readFile:") + p);
    return store_test::failRead ? String{} : store_test::files[p];
  }
  bool writeFile(const char* p, const String& s) {
    store_test::io.push_back(std::string("writeFile:") + p);
    if (store_test::failWrite) return false;
    store_test::files[p] = s;
    return true;
  }
};
inline TestStorage Storage;
