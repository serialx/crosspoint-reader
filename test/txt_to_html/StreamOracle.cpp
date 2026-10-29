#include <TxtToHtml.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
struct Reader {
  std::string bytes;
  size_t position = 0;
  size_t chunk = 8192;
  size_t calls = 0;
  size_t fail = 0;
  static int read(void* context, uint8_t* out, size_t capacity) {
    auto& r = *static_cast<Reader*>(context);
    ++r.calls;
    const size_t count = std::min({capacity, r.chunk, r.bytes.size() - r.position});
    const int result = r.calls == r.fail ? -1 : static_cast<int>(count);
    std::printf("read %zu %d\n", capacity, result);
    if (result > 0) {
      std::memcpy(out, r.bytes.data() + r.position, count);
      r.position += count;
    }
    return result;
  }
};
struct Writer : Print {
  size_t calls = 0;
  size_t fail = 0;
  std::string bytes;
  size_t write(uint8_t byte) override { return write(&byte, 1); }
  size_t write(const uint8_t* data, size_t size) override {
    ++calls;
    const size_t count = calls == fail ? size / 2 : size;
    std::printf("write %zu %zu\n", size, count);
    bytes.append(reinterpret_cast<const char*>(data), count);
    return count;
  }
};

void run(const std::string& name, const std::string& bytes, size_t chunk, size_t readFail, size_t writeFail) {
  std::printf("case %s %zu %zu %zu %zu\n", name.c_str(), bytes.size(), chunk, readFail, writeFail);
  Reader reader{bytes, 0, chunk, 0, readFail};
  Writer writer;
  writer.fail = writeFail;
  const bool ok = TxtToHtml::stream(name, &reader, Reader::read, writer);
  std::printf("result %d\n", ok);
  for (unsigned char byte : writer.bytes) std::printf("%02x", byte);
  std::puts("");
}
}  // namespace

int main() {
  std::string allBytes;
  for (unsigned i = 0; i < 256; ++i) allBytes.push_back(static_cast<char>(i));
  const std::vector<std::string> inputs = {"", "\xef\xbb\xbftext", "\xef\xbb", " a  b   \r\n  <&>\t\001\rX ",
                                          allBytes, std::string(8190, 'x') + "  &\n", std::string(9000, '&')};
  for (const auto& input : inputs) {
    for (size_t chunk : {size_t{1}, size_t{2}, size_t{3}, size_t{8192}}) {
      run("/books/a&<>.MD", input, chunk, 0, 0);
    }
    for (size_t failure = 1; failure <= (input.size() + 8191) / 8192 + 1; ++failure) {
      run("book.txt", input, 8192, failure, 0);
    }
    // Includes each flush and one non-triggered failure after the final flush.
    for (size_t failure = 1; failure <= (input.size() * 6 + 256) / 8192 + 2; ++failure) {
      run("book.txt", input, 8192, 0, failure);
    }
  }
  for (const auto* filename : {"plain", ".txt", "dir/.MD", "a.b.c.txt", "a\\b.md", "<title>.txt"}) {
    run(filename, "body", 8192, 0, 0);
  }
}
