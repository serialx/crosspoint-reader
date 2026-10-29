#include <HalStorage.h>
#include <ZipFile.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>

struct Writer : Print {
  size_t calls = 0, fail = 0;
  size_t write(uint8_t byte) override { return write(&byte, 1); }
  size_t write(const uint8_t* bytes, size_t size) override {
    ++calls;
    size_t count = calls == fail ? size / 2 : size;
    std::printf("write %zu %zu\n", size, count);
    if (size > 1000000) return 0;
    for (size_t i = 0; i < count; ++i) std::printf("%02x", bytes[i]);
    std::puts("");
    return count;
  }
};
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  std::ifstream file(argv[1], std::ios::binary);
  const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
  const std::string archivePath = "archive";
  ziptest::trace = true;
  for (const char* name : {"stored", "deflated", "absent"}) {
    for (size_t fail = 0; fail <= 12; ++fail) {
      Storage.setFile("archive", bytes); ziptest::operations = 0; ziptest::fail = fail;
      std::printf("memory %s %zu\n", name, fail);
      ZipFile zip(archivePath); size_t count = 777;
      uint8_t* data = zip.readFileToMemory(name, &count, true);
      std::printf("result %d %zu\n", data != nullptr, count);
      if (data) {
        for (size_t i = 0; i <= count; ++i) std::printf("%02x", data[i]);
        std::puts(""); std::free(data);
      }
    }
    for (size_t chunk : {size_t{1}, size_t{7}, size_t{1024}}) {
      for (bool early : {false, true}) {
        for (size_t fail = 0; fail <= 3; ++fail) {
          Storage.setFile("archive", bytes); ziptest::operations = 0; ziptest::fail = 0;
          std::printf("stream %s %zu %d %zu\n", name, chunk, early, fail);
          ZipFile zip(archivePath); Writer out; out.fail = fail;
          std::printf("result %d\n", zip.readFileToStream(name, out, chunk, early));
        }
      }
    }
  }
}
