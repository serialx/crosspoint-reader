#include <GfxRenderer.h>
#include <GifCommon.h>

#include <cstdlib>
struct Sink {
  GfxRenderer renderer;
  int writes = 0, fail;
  static void target(void* p, XpImageTarget* out) {
    auto& r = static_cast<Sink*>(p)->renderer;
    *out = {r.buffer.data(), r.buffer.size(), 66, 0, 792, 528, 792, 0, 0, 0};
  }
  static bool write(void* p, uint32_t op, const uint8_t*, size_t) {
    auto& s = *static_cast<Sink*>(p);
    return op != 1 || s.writes++ != s.fail;
  }
};
int main(int argc, char** argv) {
  if (argc != 3) return 2;
  HalFile file;
  if (!file.open(argv[1], "rb")) return 2;
  GifCommon::Source source{file};
  source.path = "fixture.gif";
  source.cachePath = "fixture.px2";
  source.targetWidth = 32;
  source.targetHeight = 16;
  auto input = source.input();
  Sink sink;
  sink.fail = std::atoi(argv[2]);
  XpImageCallbacks output{&sink, Sink::target, Sink::write};
  XpImageConfig config{0, 0, 32, 16, 528, 792, 0, 0, 1, 0, 0, 2};
  bool ok = xp_gif_frame(&input, &config, &output, true);
  std::printf("%d\n", ok);
}
