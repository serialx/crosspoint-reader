#include <DictZip.h>
#include <chrono>
#include <cstdio>

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  uint32_t request[2];
  while (fread(request, sizeof(request), 1, stdin) == 1) {
    HalFile output;
    DictZip::ExtractError error;
    const auto start = std::chrono::steady_clock::now();
    const bool ok = DictZip::extractEntry(argv[1], request[0], request[1], output, &error);
    const uint64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - start).count();
    const int32_t status = ok ? 0 : -static_cast<int32_t>(error);
    const uint32_t size = output.bytes.size();
    fwrite(&status, 4, 1, stdout);
    fwrite(&size, 4, 1, stdout);
    fwrite(&ns, 8, 1, stdout);
    fwrite(output.bytes.data(), 1, size, stdout);
  }
}
