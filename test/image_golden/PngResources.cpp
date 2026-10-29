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
struct Allocation {
  void* pointer;
  size_t size;
  size_t id;
};
std::array<Allocation, 64> allocations{};
size_t live = 0, peak = 0, largest = 0, attempts = 0, budget = SIZE_MAX;
uint64_t trace = 14695981039346656037ULL;
void event(uint64_t v) {
  for (int i = 0; i < 8; ++i) {
    trace = (trace ^ (v & 255)) * 1099511628211ULL;
    v >>= 8;
  }
}
void require(bool condition) {
  if (!condition) std::abort();
}
void reset(size_t after = SIZE_MAX) {
  require(live == 0);
  if (std::getenv("IMAGE_ALLOCATION_TRACE"))
    std::printf("trace=%016llx peak=%zu largest=%zu attempts=%zu\n", static_cast<unsigned long long>(trace), peak,
                largest, attempts);
  trace = 14695981039346656037ULL;
  peak = largest = attempts = 0;
  budget = after;
}
void* allocate(size_t size, size_t alignment) {
  ++attempts;
  event(1);
  event(attempts);
  event(size);
  if (budget != SIZE_MAX) {
    if (budget == 0) return nullptr;
    --budget;
  }
  void* result = nullptr;
  if (posix_memalign(&result, std::max(alignment, sizeof(void*)), size)) return nullptr;
  for (auto& item : allocations) {
    if (!item.pointer) {
      item = {result, size, attempts};
      live += size;
      peak = std::max(peak, live);
      largest = std::max(largest, size);
      return result;
    }
  }
  std::abort();
}
struct Context {
  std::vector<uint8_t> bytes;
  std::array<uint8_t, 792 * 528 / 8> frame{};
  size_t writes = 0, failWrite = SIZE_MAX;
  size_t diagnostics = 0;
  size_t reads = 0, shortRead = SIZE_MAX, readLimit = SIZE_MAX, readable = SIZE_MAX;
  static size_t read(void* opaque, uint32_t offset, uint8_t* out, size_t length) {
    auto& self = *static_cast<Context*>(opaque);
    const auto& bytes = self.bytes;
    require(self.reads < bytes.size() * 4 + 64);
    const bool shorten = self.reads++ == self.shortRead;
    const size_t end = std::min(bytes.size(), self.readable);
    if (offset > end) return 0;
    length = std::min(length, end - offset);
    if (shorten) length = std::min(length, self.readLimit);
    std::memcpy(out, bytes.data() + offset, length);
    return length;
  }
  static void target(void* opaque, XpImageTarget* out) {
    auto& self = *static_cast<Context*>(opaque);
    *out = {self.frame.data(), self.frame.size(), 99, 0, 528, 792, 528, 0, 0, 0};
  }
  static bool write(void* opaque, uint32_t operation, const uint8_t*, size_t) {
    auto& self = *static_cast<Context*>(opaque);
    return operation != 1 || self.writes++ != self.failWrite;
  }
  static void progress(void*) {}
  static constexpr bool allocationFailure(uint32_t event) {
    if (event == XP_IMAGE_DIAG_RowAllocation || event == XP_IMAGE_DIAG_CacheAllocation) return true;
#ifdef RESOURCE_GIF
    return event == XP_GIF_DIAG_BmpRows || event == XP_GIF_DIAG_Scaling || event == XP_GIF_DIAG_Atkinson1 ||
           event == XP_GIF_DIAG_Atkinson2 || event == XP_GIF_DIAG_DecoderAlloc || event == XP_GIF_DIAG_PipelineAlloc ||
           event == XP_GIF_DIAG_Cache;
#else
    return event == XP_PNG_DIAG_Scanline || event == XP_PNG_DIAG_Inflate || event == XP_PNG_DIAG_RowScratch ||
           event == XP_PNG_DIAG_Atkinson1 || event == XP_PNG_DIAG_Atkinson2 || event == XP_PNG_DIAG_Accumulators ||
           event == XP_PNG_DIAG_DecoderAlloc || event == XP_PNG_DIAG_PipelineAlloc;
#endif
  }
  static void diagnostic(void* opaque, uint32_t event, uint32_t, uint32_t, uint32_t, uint32_t) {
    if (allocationFailure(event) && !std::getenv("DROP_ALLOCATION_DIAGNOSTICS"))
      ++static_cast<Context*>(opaque)->diagnostics;
  }
  static bool bmpWrite(void* opaque, const uint8_t* bytes, size_t len) { return write(opaque, 1, bytes, len); }
  XpPngServices services() {
    return {this, read, target, write, progress, static_cast<uint32_t>(bytes.size()), diagnostic};
  }
};
}  // namespace

extern "C" void* heap_caps_malloc(size_t size, uint32_t) { return allocate(size, 4); }
extern "C" void* heap_caps_aligned_alloc(size_t align, size_t size, uint32_t) { return allocate(size, align); }
extern "C" void heap_caps_free(void* pointer) {
  if (!pointer) return;
  for (auto& item : allocations) {
    if (item.pointer == pointer) {
      event(2);
      event(item.id);
      live -= item.size;
      item = {};
      std::free(pointer);
      return;
    }
  }
  std::abort();
}
extern "C" void* heap_caps_realloc(void* pointer, size_t size, uint32_t) {
  if (!pointer) return allocate(size, 4);
  for (const auto& item : allocations) {
    if (item.pointer == pointer) {
      auto* result = allocate(size, 4);
      if (result) {
        std::memcpy(result, pointer, std::min(size, item.size));
        heap_caps_free(pointer);
      }
      return result;
    }
  }
  std::abort();
}
extern "C" void xp_chapter_release_block(void*) { std::abort(); }

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  Context context;
  std::ifstream file(argv[1], std::ios::binary);
  context.bytes.assign(std::istreambuf_iterator<char>(file), {});
  auto services = context.services();
  const XpImageConfig config{0, 0, 528, 792, 528, 792, 0, 0, 0, 1, 0, 0};
  const XpPngBmpConfig bmp{37, 53, true, true, false};
  alignas(uintptr_t) static uint8_t scratch[60000];
  require(xp_png_scratch_size() <= sizeof(scratch));
#ifdef RESOURCE_GIF
  const XpGifInput input{&context, Context::read, Context::progress, Context::diagnostic};
  const XpGifBmpConfig gifBmp{37, 53, 1, 1, 0};
  const XpGifOutput output{&context, Context::bmpWrite};
  const XpImageCallbacks callbacks{&context, Context::target, Context::write, Context::diagnostic};
  constexpr int modes = 2;
#else
  constexpr int modes = 3;
#endif
  for (int mode = 0; mode < modes; ++mode) {
    const auto run = [&]() {
      context.reads = 0;
      context.diagnostics = 0;
#ifdef RESOURCE_GIF
      return mode == 0 ? xp_gif_frame(&input, &config, &callbacks, true) : xp_gif_bmp(&input, &gifBmp, &output);
#else
      return mode == 0 ? xp_png_render(&services, &config, true)
                       : xp_png_bmp(&services, &bmp, mode == 2 ? scratch : nullptr, mode == 2 ? sizeof(scratch) : 0);
#endif
    };
    reset();
    const bool success = run();
    const size_t count = attempts;
    const size_t reads = context.reads;
    require(live == 0);
    std::printf("mode=%d success=%d allocations=%zu peak=%zu largest=%zu scratch=%zu\n", mode, success, count, peak,
                largest, mode == 2 ? xp_png_scratch_size() : 0);
    if (!success) continue;
    for (size_t after = 0; after < count; ++after) {
      reset(after);
      const bool ok = run();
      require(live == 0);
      // Frame cache allocation is optional; every required allocation must fail cleanly.
      require(!ok || (mode == 0 && after + 1 == count));
      if (!ok && std::getenv("VERIFY_DIAGNOSTICS") && context.diagnostics == 0) {
        std::fprintf(stderr, "Missing allocation diagnostic: mode=%d allocation=%zu/%zu\n", mode, after, count);
        require(false);
      }
    }
    reset();
    context.writes = 0;
    context.failWrite = 0;
    if (mode != 0) require(!run());
    require(live == 0);
    context.failWrite = SIZE_MAX;
    // Inject EOF at every byte while preserving the advertised file length.
    for (size_t end = 0; end < context.bytes.size(); ++end) {
      reset();
      context.readable = end;
      run();
      require(live == 0);
    }
    context.readable = SIZE_MAX;
    for (size_t call = 0; call < reads; ++call) {
      for (size_t limit : {size_t(0), size_t(1), size_t(7)}) {
        reset();
        context.shortRead = call;
        context.readLimit = limit;
        run();
        require(live == 0);
      }
    }
    context.shortRead = SIZE_MAX;
    reset();
    require(run());
    require(live == 0);
  }
  reset();
}

// The raw archive also contains asset code; keep its platform domain paired
// with this resource probe's allocation accounting.
extern "C" void* xp_assets_platform_alloc(size_t size, uint32_t caps) {
  require(caps == 3);
  return heap_caps_malloc(size, 0);
}
extern "C" void* xp_assets_platform_realloc(void* data, size_t size, uint32_t caps) {
  require(caps == 3);
  return heap_caps_realloc(data, size, 0);
}
extern "C" void xp_assets_platform_free(void* data) { heap_caps_free(data); }
