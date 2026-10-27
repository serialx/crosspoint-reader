#include <xp_ffi.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

namespace {
struct Context {
  std::vector<uint8_t> bytes;
  std::array<uint8_t, 64> pixels{};
  size_t calls = 0, readBytes = 0;
  uint64_t hash = 14695981039346656037ULL;
  static size_t read(void* raw, uint32_t offset, uint8_t* out, size_t len) {
    auto& self = *static_cast<Context*>(raw);
    ++self.calls;
    len = offset < self.bytes.size() ? std::min(len, self.bytes.size() - offset) : 0;
    if (len) std::memcpy(out, self.bytes.data() + offset, len);
    self.readBytes += len;
    return len;
  }
  static void poll(void*) {}
  static void diagnostic(void*, uint32_t event, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    std::printf("event %u %u %u %u %u\n", event, a, b, c, d);
  }
  static void target(void* raw, XpImageTarget* out) {
    auto& self = *static_cast<Context*>(raw);
    *out = {self.pixels.data(), self.pixels.size(), 4, 0, 16, 32, 16, 0, 0, 0};
  }
  static bool io(void*, uint32_t, const uint8_t*, size_t) { return true; }
  static bool write(void* raw, const uint8_t* bytes, size_t len) {
    auto& self = *static_cast<Context*>(raw);
    for (size_t i = 0; i < len; ++i) self.hash = (self.hash ^ bytes[i]) * 1099511628211ULL;
    return true;
  }
};
}  // namespace

int main(int argc, char** argv) {
  if (argc != 4) return 2;
  Context context;
  std::ifstream file(argv[1], std::ios::binary);
  context.bytes.assign(std::istreambuf_iterator<char>(file), {});
  const XpGifInput input{&context, Context::read, Context::poll, std::atoi(argv[3]) ? Context::diagnostic : nullptr};
  bool ok;
  if (std::atoi(argv[2])) {
    const XpGifBmpConfig config{8, 8, 1, 0, 0};
    const XpGifOutput output{&context, Context::write};
    ok = xp_gif_bmp(&input, &config, &output);
  } else {
    const XpImageConfig config{0, 0, 8, 8, 16, 32, 0, 0, 0, 0, 0, 2};
    const XpImageCallbacks output{&context, Context::target, Context::io};
    ok = xp_gif_frame(&input, &config, &output, false);
    Context::write(&context, context.pixels.data(), context.pixels.size());
  }
  std::printf("result %d %zu %zu %016llx\n", ok, context.calls, context.readBytes,
              static_cast<unsigned long long>(context.hash));
}
