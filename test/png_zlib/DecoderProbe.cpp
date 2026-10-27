#include <PNGdec.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <memory>
#include <vector>

struct Rows {
  unsigned count = 0;
  uint32_t hash = 2166136261;
  FILE* dump = nullptr;
};

static int draw(PNGDRAW* row) {
  auto* rows = static_cast<Rows*>(row->pUser);
  ++rows->count;
  if (rows->dump) std::fwrite(row->pPixels, 1, row->iPitch, rows->dump);
  for (int i = 0; i < row->iPitch; ++i) rows->hash = (rows->hash ^ row->pPixels[i]) * 16777619;
  return 1;
}

int main(int argc, char** argv) {
  if (argc != 4) return 2;
  std::ifstream file(argv[1], std::ios::binary);
  if (!file) return 2;
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
  auto png = std::make_unique<PNG>();
  const int repeats = std::atoi(argv[2]);
  const int options = std::atoi(argv[3]);
  Rows rows;
  int opened = 0, decoded = -1;
  double micros = 0;
  for (int i = 0; i < repeats; ++i) {
    rows = {};
    opened = png->openRAM(bytes.data(), bytes.size(), draw);
    if (opened) break;
    // Mirror the production guard before allowing PNGdec to touch its rows.
    int channels = png->getPixelType() == PNG_PIXEL_TRUECOLOR         ? 3
                   : png->getPixelType() == PNG_PIXEL_TRUECOLOR_ALPHA ? 4
                   : png->getPixelType() == PNG_PIXEL_GRAY_ALPHA      ? 2
                                                                      : 1;
    if (2 * ((png->getWidth() * channels * png->getBpp() + 7) / 8 + 1) + 32 > PNG_MAX_BUFFERED_PIXELS) {
      decoded = -2;
      break;
    }
    if (const char* path = std::getenv("PNG_PROBE_ROWS")) rows.dump = std::fopen(path, "wb");
    const auto start = std::chrono::steady_clock::now();
    decoded = png->decode(&rows, options);
    micros += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
    png->close();
    if (rows.dump) std::fclose(rows.dump);
  }
  std::printf("{\"open\":%d,\"decode\":%d,\"rows\":%u,\"hash\":%u,\"us\":%.3f,\"sizeof_png\":%zu}\n", opened, decoded,
              rows.count, rows.hash, micros / repeats, sizeof(PNG));
}
