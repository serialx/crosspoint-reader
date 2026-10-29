#pragma once
#include <memory>
#include <string>
namespace freeink::content {
struct ContentDecryptor {
  bool isEncrypted(const std::string&) const { return false; }
  size_t decryptedSize(const std::string&) const { return 0; }
  bool decryptToSink(const std::string&, bool (*)(void*, const uint8_t*, size_t), void*) const { return false; }
};
inline std::unique_ptr<ContentDecryptor> openProtectedBook(const std::string&, std::string&) { return {}; }
}  // namespace freeink::content
