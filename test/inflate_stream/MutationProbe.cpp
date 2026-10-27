#include <MinizConfig.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

int main(int argc, char** argv) {
  if (argc != 3) return 2;
  FILE* input = fopen(argv[1], "rb");
  FILE* output = fopen(argv[2], "wb");
  if (!input || !output) return 2;
  uint32_t header[4];
  while (fread(header, sizeof(header), 1, input) == 1) {
    std::vector<unsigned char> bytes(header[3]);
    if (fread(bytes.data(), 1, bytes.size(), input) != bytes.size()) return 3;
    mz_stream stream{};
    if (mz_inflateInit2(&stream, header[0] ? 15 : -15) != MZ_OK) return 4;
    unsigned char buffer[4096];
    uint32_t hash = 2166136261u;
    size_t supplied = 0;
    int status = 0;
    for (unsigned call = 0; call < 300000; ++call) {
      if (!stream.avail_in && supplied < bytes.size()) {
        stream.next_in = bytes.data() + supplied;
        stream.avail_in = std::min<size_t>(header[1], bytes.size() - supplied);
        supplied += stream.avail_in;
      }
      stream.next_out = buffer;
      stream.avail_out = header[2];
      const auto before = stream.total_in;
      status = mz_inflate(&stream, MZ_NO_FLUSH);
      for (unsigned i = 0; i < header[2] - stream.avail_out; ++i) hash = (hash ^ buffer[i]) * 16777619u;
      if (status == MZ_STREAM_END || (status < 0 && status != MZ_BUF_ERROR)) break;
      if (stream.total_out >= 131072) {
        status = 99;
        break;
      }
      if (before == stream.total_in && stream.avail_out == header[2] && supplied == bytes.size()) break;
      if (call == 299999) return 5;
    }
    uint32_t result[] = {static_cast<uint32_t>(status), static_cast<uint32_t>(stream.total_in),
                         static_cast<uint32_t>(stream.total_out), hash, static_cast<uint32_t>(stream.adler)};
    if (fwrite(result, sizeof(result), 1, output) != 1) return 6;
    mz_inflateEnd(&stream);
  }
  fclose(input);
  fclose(output);
}
