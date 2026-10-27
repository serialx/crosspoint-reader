#include <DictZip.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
  if (argc < 2 || argc > 3) return 2;
  const unsigned iterations = argc == 3 ? std::strtoul(argv[2], nullptr, 10) : 1;
  if (!iterations || iterations > 10000) return 2;
  uint32_t request[2];
  while (fread(request, sizeof(request), 1, stdin) == 1) {
    HalFile output;
    DictZip::ExtractError error;
    const auto start = std::chrono::steady_clock::now();
    bool ok = false;
    for (unsigned i = 0; i < iterations; ++i) {
      output.bytes.clear();
      ok = DictZip::extractEntry(argv[1], request[0], request[1], output, &error);
      if (!ok) break;
    }
    const uint64_t ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
    const int32_t status = ok ? 0 : -static_cast<int32_t>(error);
    const uint32_t size = output.bytes.size();
    fwrite(&status, 4, 1, stdout);
    fwrite(&size, 4, 1, stdout);
    fwrite(&ns, 8, 1, stdout);
    fwrite(output.bytes.data(), 1, size, stdout);
  }
}
