#pragma once
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
namespace freeink::content {
struct ContentDecryptor {
  bool isEncrypted(const std::string& path) const { return path == "secret"; }
  size_t decryptedSize(const std::string&) const {
    const char* size = std::getenv("BOOK_PLAIN_SIZE");
    return size ? std::strtoull(size, nullptr, 10) : 7;
  }
  bool decryptToSink(const std::string&, bool (*sink)(void*, const uint8_t*, size_t), void* context) const {
    const uint8_t data[]{'p', 'a', 'y', 'l', 'o', 'a', 'd'};
    return sink(context, data, 3) && sink(context, data + 3, 4) && !std::getenv("BOOK_DECRYPT_FAIL");
  }
};
inline std::unique_ptr<ContentDecryptor> openProtectedBook(const std::string&, std::string& error) {
  if (std::getenv("BOOK_KEY_ERROR")) error = "missing-key";
  return std::getenv("BOOK_PROTECTED") ? std::make_unique<ContentDecryptor>() : nullptr;
}
}  // namespace freeink::content
