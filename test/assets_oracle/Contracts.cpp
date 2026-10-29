#include <QrUtils.h>
#include <xp_ffi.h>

#include <array>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <vector>

extern "C" size_t asset_allocation_attempts();
extern "C" size_t asset_platform_live();
extern "C" size_t asset_platform_largest();
struct Sink {
  size_t calls = 0, fail = SIZE_MAX;
  std::vector<uint8_t> bytes;
  static bool write(void* raw, const uint8_t* bytes, size_t size) {
    auto& self = *static_cast<Sink*>(raw);
    if (self.calls++ == self.fail) return false;
    self.bytes.insert(self.bytes.end(), bytes, bytes + size);
    return true;
  }
};
void jpegPlatformBuffers() {
  static const std::array<uint8_t, 2048 * 16> grayscale{};
  XpAssetJpegConfig config{2048, 16, false, false, false};
  auto run = [&]() {
    Sink sink;
    const XpAssetJpegServices services{
        &sink, [](void* ctx, const uint8_t* bytes, size_t len) { Sink::write(ctx, bytes, len); }, [](void*) {},
        [](void*, uint32_t, int32_t, int32_t, int32_t, int32_t) {},
        [](void*, void* state, XpAssetJpegBlock block) {
          return block(state, 0, 0, 2048, 16, 2048, grayscale.data());
        }};
    return xp_assets_jpeg_bmp(2048, 16, false, &config, &services);
  };
  const size_t before = asset_allocation_attempts();
  assert(run());
  const size_t allocations = asset_allocation_attempts() - before;
  assert(allocations == 3 && asset_platform_largest() == 32768 && asset_platform_live() == 0);
  for (size_t i = 0; i < allocations; ++i) {
    const auto index = std::to_string(asset_allocation_attempts() + i);
    assert(setenv("ASSET_FAIL_ALLOC", index.c_str(), 1) == 0);
    assert(!run() && asset_platform_live() == 0);
    assert(unsetenv("ASSET_FAIL_ALLOC") == 0);
    assert(run() && asset_platform_live() == 0);
  }
}
int main() {
  jpegPlatformBuffers();
  std::string text = "https://crosspointreader.com";
  uint8_t version = 0;
  size_t payload = 0;
  const size_t capacity =
      xp_assets_qr_plan(reinterpret_cast<const uint8_t*>(text.data()), text.size(), &version, &payload);
  std::vector<uint8_t> bitmap(capacity + 2, 0xa5);
  uint8_t modules = 99;
  const size_t allocations = asset_allocation_attempts();
  assert(!QrUtils::encodeQrBitmap(text, bitmap.data() + 1, capacity - 1, modules));
  assert(modules == 0);
  for (uint8_t value : bitmap) assert(value == 0xa5);
  assert(QrUtils::encodeQrBitmap(text, bitmap.data() + 1, capacity, modules));
  assert(modules == 33 && bitmap.front() == 0xa5 && bitmap.back() == 0xa5);
  assert(asset_allocation_attempts() == allocations);
  std::array<uint8_t, 18> frame{};
  for (size_t i = 0; i < frame.size(); ++i) frame[i] = static_cast<uint8_t>(i * 19);
  Sink complete;
  assert(xp_assets_screenshot(frame.data(), frame.size(), 16, 9, &complete, Sink::write) == 0);
  assert(complete.calls == 17 && complete.bytes.size() == 126);
  for (size_t failure = 0; failure < complete.calls; ++failure) {
    Sink sink;
    sink.fail = failure;
    assert(xp_assets_screenshot(frame.data(), frame.size(), 16, 9, &sink, Sink::write) == 1);
    assert(sink.calls == failure + 1);
    const size_t prefix = failure == 0 ? 0 : 62 + (failure - 1) * 4;
    assert(sink.bytes.size() == prefix);
    if (prefix) assert(std::memcmp(sink.bytes.data(), complete.bytes.data(), prefix) == 0);
  }
  Sink invalid;
  assert(xp_assets_screenshot(frame.data(), frame.size() - 1, 16, 9, &invalid, Sink::write) == 1);
  assert(invalid.calls == 0);
}
