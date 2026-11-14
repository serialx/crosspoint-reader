// JPEGDEC header-guard cases. Every input is generated here from a small baseline image encoded by this file.
// One case runs per process (a negative control may abort); run_cases.py compares the decoder variants.
#include <JPEGDEC.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "observer.h"

using Bytes = std::vector<uint8_t>;
constexpr double kPi = 3.14159265358979323846;

// ---------------------------------------------------------------- observer (used by *_observed variants)
int jhg_limit = 0;
int jhg_sos = 0;
static std::map<std::string, int> g_hits;
void jhg_hit(const char* site) {
  if (g_hits[site]++ == 0) {
    std::fprintf(stderr, "JHG_HIT %s\n", site);  // flushed before any later crash
    std::fflush(stderr);
  }
}

// ---------------------------------------------------------------- encoder
namespace {

struct Segment {
  uint8_t marker;
  Bytes payload;
  int lengthOverride = -1;
};

struct Jpeg {
  std::vector<Segment> segments;  // everything between SOI and the entropy data, SOS last
  Bytes scan;                     // entropy-coded data
  Bytes tail = {0xff, 0xd9};      // EOI and anything after it

  Bytes bytes() const {
    Bytes out = {0xff, 0xd8};
    for (const auto& s : segments) {
      out.push_back(0xff);
      out.push_back(s.marker);
      const int len = s.lengthOverride >= 0 ? s.lengthOverride : int(s.payload.size()) + 2;
      out.push_back(uint8_t(len >> 8));
      out.push_back(uint8_t(len));
      out.insert(out.end(), s.payload.begin(), s.payload.end());
    }
    out.insert(out.end(), scan.begin(), scan.end());
    out.insert(out.end(), tail.begin(), tail.end());
    return out;
  }
  Segment& find(uint8_t marker, int nth = 0) {
    for (auto& s : segments)
      if (s.marker == marker && nth-- == 0) return s;
    std::fprintf(stderr, "no segment %02x\n", marker);
    std::exit(3);
  }
  size_t index(uint8_t marker) const {
    for (size_t i = 0; i < segments.size(); i++)
      if (segments[i].marker == marker) return i;
    std::exit(3);
  }
  // File offset of segment i's 0xFF marker byte.
  size_t offsetOf(size_t i) const {
    size_t off = 2;
    for (size_t k = 0; k < i; k++) off += 4 + segments[k].payload.size();
    return off;
  }
};

struct Options {
  int components = 3;
  int width = 24, height = 16;
  bool combinedTables = false;
  bool wordQuant = false;
  int restartInterval = 0;
  bool progressiveDc = false;
};

int zigzag[64];
void initZigzag() {
  int k = 0;
  for (int s = 0; s < 15; s++) {
    if (s % 2 == 0) {
      for (int r = std::min(s, 7); r >= std::max(0, s - 7); r--) zigzag[k++] = r * 8 + (s - r);
    } else {
      for (int r = std::max(0, s - 7); r <= std::min(s, 7); r++) zigzag[k++] = r * 8 + (s - r);
    }
  }
}

struct Huff {
  uint8_t bits[16] = {};
  Bytes vals;
  uint16_t code[256] = {};
  uint8_t size[256] = {};
  void build() {
    int c = 0, k = 0;
    for (int len = 1; len <= 16; len++) {
      for (int i = 0; i < bits[len - 1]; i++) {
        code[vals[k]] = uint16_t(c++);
        size[vals[k++]] = uint8_t(len);
      }
      c <<= 1;
    }
  }
};

// DC: twelve 4-bit codes. AC: EOB and 0x01 get 2-bit codes, the other 160 symbols 9-bit codes.
Huff dcTable() {
  Huff h;
  h.bits[3] = 12;
  for (int i = 0; i < 12; i++) h.vals.push_back(uint8_t(i));
  h.build();
  return h;
}
Huff acTable() {
  Huff h;
  h.bits[1] = 2;
  h.bits[8] = 160;
  h.vals = {0x00, 0x01};
  for (int r = 0; r < 16; r++)
    for (int s = 1; s <= 10; s++)
      if ((r << 4 | s) != 0x01) h.vals.push_back(uint8_t(r << 4 | s));
  h.vals.push_back(0xf0);
  h.build();
  return h;
}

Bytes huffPayload(uint8_t id, const Huff& h) {
  Bytes p = {id};
  p.insert(p.end(), h.bits, h.bits + 16);
  p.insert(p.end(), h.vals.begin(), h.vals.end());
  return p;
}

struct BitWriter {
  Bytes out;
  uint32_t acc = 0;
  int n = 0;
  void put(uint32_t value, int bits) {
    for (int i = bits - 1; i >= 0; i--) {
      acc = (acc << 1) | ((value >> i) & 1);
      if (++n == 8) {
        out.push_back(uint8_t(acc));
        if (uint8_t(acc) == 0xff) out.push_back(0);
        acc = 0;
        n = 0;
      }
    }
  }
  void flush() {
    while (n) put(1, 1);
  }
};

int category(int v) {
  v = std::abs(v);
  int c = 0;
  while (v) {
    c++;
    v >>= 1;
  }
  return c;
}
void putValue(BitWriter& w, const Huff& h, int symbol, int value, int cat) {
  w.put(h.code[symbol], h.size[symbol]);
  if (cat) w.put(uint32_t(value < 0 ? value - 1 : value) & ((1u << cat) - 1), cat);
}

int sample(int comp, int x, int y) {
  if (comp == 0) return (x * 9 + y * 13 + (x * y) % 17) & 0xff;
  if (comp == 1) return 128 + ((x - y) * 3) % 60;
  return 100 + (x * 5) % 70;
}

Jpeg encode(const Options& o) {
  initZigzag();
  const Huff dc = dcTable(), ac = acTable();
  Jpeg j;
  j.segments.push_back({0xe0, {'J', 'F', 'I', 'F', 0, 1, 1, 0, 0, 1, 0, 1, 0, 0}});
  int quant[2][64];
  for (int t = 0; t < 2; t++)
    for (int k = 0; k < 64; k++) quant[t][k] = (o.wordQuant ? 300 : 8) + k + t * 4;
  Bytes dqt[2];
  for (int t = 0; t < 2; t++) {
    dqt[t].push_back(uint8_t((o.wordQuant ? 0x10 : 0) | t));
    for (int k = 0; k < 64; k++) {
      if (o.wordQuant) dqt[t].push_back(uint8_t(quant[t][k] >> 8));
      dqt[t].push_back(uint8_t(quant[t][k]));
    }
  }
  const int tables = o.components == 1 ? 1 : 2;
  if (o.combinedTables) {
    Bytes p;
    for (int t = 0; t < tables; t++) p.insert(p.end(), dqt[t].begin(), dqt[t].end());
    j.segments.push_back({0xdb, p});
  } else {
    for (int t = 0; t < tables; t++) j.segments.push_back({0xdb, dqt[t]});
  }
  Bytes sof = {
      8, uint8_t(o.height >> 8), uint8_t(o.height), uint8_t(o.width >> 8), uint8_t(o.width), uint8_t(o.components)};
  for (int c = 0; c < o.components; c++) {
    sof.push_back(uint8_t(c + 1));
    sof.push_back(0x11);
    sof.push_back(uint8_t(c ? 1 : 0));
  }
  j.segments.push_back({uint8_t(o.progressiveDc ? 0xc2 : 0xc0), sof});
  Bytes dht;
  for (int t = 0; t < tables; t++) {
    const Bytes d = huffPayload(uint8_t(t), dc), a = huffPayload(uint8_t(0x10 | t), ac);
    if (o.combinedTables) {
      dht.insert(dht.end(), d.begin(), d.end());
      dht.insert(dht.end(), a.begin(), a.end());
    } else {
      j.segments.push_back({0xc4, d});
      j.segments.push_back({0xc4, a});
    }
  }
  if (o.combinedTables) j.segments.push_back({0xc4, dht});
  if (o.restartInterval) j.segments.push_back({0xdd, {uint8_t(o.restartInterval >> 8), uint8_t(o.restartInterval)}});
  Bytes sos = {uint8_t(o.components)};
  for (int c = 0; c < o.components; c++) {
    sos.push_back(uint8_t(c + 1));
    sos.push_back(c ? 0x11 : 0x00);
  }
  sos.insert(sos.end(), {0, uint8_t(o.progressiveDc ? 0 : 63), 0});
  j.segments.push_back({0xda, sos});

  BitWriter w;
  int pred[3] = {};
  const int mcusX = o.width / 8, mcusY = o.height / 8;
  int mcu = 0, rst = 0;
  for (int my = 0; my < mcusY; my++) {
    for (int mx = 0; mx < mcusX; mx++) {
      if (o.restartInterval && mcu && mcu % o.restartInterval == 0) {
        w.flush();
        w.out.push_back(0xff);
        w.out.push_back(uint8_t(0xd0 + rst));
        rst = (rst + 1) & 7;
        pred[0] = pred[1] = pred[2] = 0;
      }
      mcu++;
      for (int c = 0; c < o.components; c++) {
        double block[64];
        for (int v = 0; v < 8; v++)
          for (int u = 0; u < 8; u++) {
            double sum = 0;
            for (int y = 0; y < 8; y++)
              for (int x = 0; x < 8; x++)
                sum += (sample(c, mx * 8 + x, my * 8 + y) - 128) * std::cos((2 * x + 1) * u * kPi / 16) *
                       std::cos((2 * y + 1) * v * kPi / 16);
            block[v * 8 + u] = sum * (u ? 1.0 : std::sqrt(0.5)) * (v ? 1.0 : std::sqrt(0.5)) / 4;
          }
        int coef[64];
        for (int k = 0; k < 64; k++) coef[k] = int(std::lround(block[zigzag[k]] / quant[c ? 1 : 0][k]));
        const int diff = coef[0] - pred[c];
        pred[c] = coef[0];
        putValue(w, dc, category(diff), diff, category(diff));
        if (o.progressiveDc) continue;
        int run = 0;
        for (int k = 1; k < 64; k++) {
          if (!coef[k]) {
            run++;
            continue;
          }
          for (; run > 15; run -= 16) putValue(w, ac, 0xf0, 0, 0);
          const int cat = category(coef[k]);
          putValue(w, ac, run << 4 | cat, coef[k], cat);
          run = 0;
        }
        if (run) putValue(w, ac, 0x00, 0, 0);
      }
    }
  }
  w.flush();
  j.scan = w.out;
  return j;
}

// ---------------------------------------------------------------- EXIF and padding helpers
struct Tag {
  uint16_t tag, type;
  uint32_t count, value;
};
void put16(Bytes& b, bool mm, uint32_t v) {
  if (mm)
    b.insert(b.end(), {uint8_t(v >> 8), uint8_t(v)});
  else
    b.insert(b.end(), {uint8_t(v), uint8_t(v >> 8)});
}
void put32(Bytes& b, bool mm, uint32_t v) {
  if (mm)
    b.insert(b.end(), {uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)});
  else
    b.insert(b.end(), {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)});
}
void putTag(Bytes& b, bool mm, const Tag& t) {
  put16(b, mm, t.tag);
  put16(b, mm, t.type);
  put32(b, mm, t.count);
  if (t.type == 3 && t.count == 1) {  // short value, left-justified
    put16(b, mm, t.value);
    put16(b, mm, 0);
  } else {
    put32(b, mm, t.value);
  }
}
struct Exif {
  bool mm = true;
  std::vector<Tag> ifd0 = {{274, 3, 1, 6}};
  std::vector<Tag> ifd1;
  int64_t ifd0Offset = -1;  // -1: 8
  int64_t nextIfd = -1;     // -1: right after IFD0 when ifd1 is present, else 0
  size_t padTo = 0;         // total APP1 payload size
};
Bytes exifPayload(const Exif& e) {
  Bytes b = {'E', 'x', 'i', 'f', 0, 0};
  b.insert(b.end(),
           e.mm ? std::initializer_list<uint8_t>{'M', 'M', 0, 42} : std::initializer_list<uint8_t>{'I', 'I', 42, 0});
  put32(b, e.mm, e.ifd0Offset < 0 ? 8 : uint32_t(e.ifd0Offset));
  put16(b, e.mm, uint32_t(e.ifd0.size()));
  for (const auto& t : e.ifd0) putTag(b, e.mm, t);
  const uint32_t ifd1At = uint32_t(8 + 2 + 12 * e.ifd0.size() + 4);
  put32(b, e.mm, e.nextIfd >= 0 ? uint32_t(e.nextIfd) : (e.ifd1.empty() ? 0 : ifd1At));
  if (!e.ifd1.empty()) {
    put16(b, e.mm, uint32_t(e.ifd1.size()));
    for (const auto& t : e.ifd1) putTag(b, e.mm, t);
    put32(b, e.mm, 0);
  }
  if (b.size() < e.padTo) b.resize(e.padTo, 0x5a);
  return b;
}
const std::vector<Tag> kThumbTags = {{256, 3, 1, 40}, {257, 3, 1, 30}, {513, 4, 1, 400}, {514, 4, 1, 100}};

Segment comment(size_t payload) { return {0xfe, Bytes(payload, 0x20)}; }

// Insert a COM segment before segment `before` so that its marker lands at file offset `target`.
void placeAt(Jpeg& j, size_t before, size_t target) {
  const size_t at = j.offsetOf(before);
  if (target < at + 4) std::exit(4);
  j.segments.insert(j.segments.begin() + long(before), comment(target - at - 4));
}

Bytes base() { return encode({}).bytes(); }

// ---------------------------------------------------------------- cases
struct Case {
  std::string name;
  const char* kind;     // malformed: rejected; exif: bad EXIF skipped; valid: decodes; unusual: as control, or rejected
                        // where the control accepted a header without a usable frame (0x0)
  const char* guard;    // patch hunk expected to act (malformed and exif cases)
  const char* control;  // observer site(s) the unpatched decoder must reach, comma separated
  const char* reference;  // exif cases: case whose pixels must match
  std::function<Bytes()> make;
};

Jpeg baseJpeg() { return encode({}); }

std::vector<Case> cases() {
  std::vector<Case> c;
  auto add = [&](std::string n, const char* k, const char* g, const char* ctl, const char* ref,
                 std::function<Bytes()> f) { c.push_back({n, k, g, ctl, ref, std::move(f)}); };
  // Frame header component count.
  for (int count : {0, 5, 255}) {
    add("sof_components_" + std::to_string(count), "malformed", "sof", count ? "sof_count,jpci_overflow" : "sof_count",
        "", [count] {
          Jpeg j = baseJpeg();
          Segment& s = j.find(0xc0);
          s.payload.resize(6);
          s.payload[5] = uint8_t(count);
          for (int i = 0; i < count; i++) s.payload.insert(s.payload.end(), {uint8_t(i + 1), 0x11, 0});
          return j.bytes();
        });
  }
  add("sof_length_short", "malformed", "sof", "sof_length_underflow", "", [] {
    Jpeg j = baseJpeg();
    j.find(0xc0).payload.resize(6 + 6);  // three components declared, two present
    return j.bytes();
  });
  // Segment lengths.
  add("zero_length_dht", "malformed", "segment_length", "segment_short", "", [] {
    Jpeg j = baseJpeg();
    j.find(0xc4).lengthOverride = 0;
    return j.bytes();
  });
  add("zero_length_app0", "malformed", "segment_length", "segment_short", "", [] {
    Jpeg j = baseJpeg();
    j.find(0xe0).lengthOverride = 0;
    return j.bytes();
  });
  add("one_length_dqt", "malformed", "segment_length", "segment_short", "", [] {
    Jpeg j = baseJpeg();
    j.find(0xdb).lengthOverride = 1;
    return j.bytes();
  });
  add("length_past_eof_com", "malformed", "segment_length", "segment_past_eof", "", [] {
    Jpeg j = baseJpeg();
    j.segments.insert(j.segments.begin() + 1, Segment{0xfe, Bytes(8, 0x20), 0xfff0});
    return j.bytes();
  });
  add("length_past_eof_late", "malformed", "segment_length", "segment_past_eof,marker_unread", "", [] {
    // Late in a 71 KB file the unguarded loop keeps going, seeks past the end and parses stale buffer bytes.
    Jpeg j = baseJpeg();
    j.segments.insert(j.segments.begin() + 1, Segment{0xe2, Bytes(60000, 0xa5)});
    j.segments.insert(j.segments.begin() + 2, Segment{0xe2, Bytes(10000, 0xa5)});
    j.segments.insert(j.segments.begin() + 3, Segment{0xfe, Bytes(8, 0x20), 0xfff0});
    return j.bytes();
  });
  add("truncated_dht", "malformed", "segment_length", "segment_past_eof,segment_past_read", "", [] {
    Jpeg j = baseJpeg();
    const size_t i = j.index(0xc4) + 1;  // first AC table
    Bytes b = j.bytes();
    b.resize(j.offsetOf(i) + 4 + 60);
    return b;
  });
  add("truncated_sos", "malformed", "segment_length", "segment_past_eof,segment_past_read", "", [] {
    Jpeg j = baseJpeg();
    Bytes b = j.bytes();
    b.resize(j.offsetOf(j.index(0xda)) + 6);
    return b;
  });
  add("truncated_after_marker", "malformed", "marker_read", "marker_unread", "", [] {
    Jpeg j = baseJpeg();
    Bytes b = j.bytes();
    b.resize(j.offsetOf(j.index(0xda)) + 2);  // FF DA and nothing more
    return b;
  });
  add("dht_past_window", "malformed", "segment_fit", "segment_past_read", "", [] {
    // A 1,500-byte DHT whose marker sits at window offset 1,000: inside the file, beyond the 2,048-byte buffer.
    Jpeg j = baseJpeg();
    const size_t i = j.index(0xc4);
    Segment& s = j.segments[i];
    const Huff ac = acTable();
    while (s.payload.size() < 1500) {
      const Bytes a = huffPayload(0x10, ac);
      s.payload.insert(s.payload.end(), a.begin(), a.end());
    }
    placeAt(j, i, 1000);
    return j.bytes();
  });
  // Table ids and remaining-length arithmetic.
  add("dht_table_id_05", "malformed", "dht_id", "dht_table_id", "", [] {
    Jpeg j = baseJpeg();
    j.find(0xc4, 2).payload[0] = 0x05;
    return j.bytes();
  });
  add("dht_table_id_ff", "malformed", "dht_id", "dht_table_id", "", [] {
    Jpeg j = baseJpeg();
    j.find(0xc4, 1).payload[0] = 0xff;
    return j.bytes();
  });
  add("dqt_underflow", "malformed", "dqt", "dqt_underflow", "", [] {
    Jpeg j = baseJpeg();
    Segment& s = j.find(0xdb);
    s.payload.insert(s.payload.end(), {1, 9, 9, 9, 9, 9, 9, 9, 9, 9});  // a second table cut to ten bytes
    return j.bytes();
  });
  add("dqt_underflow_word", "malformed", "dqt", "dqt_underflow", "", [] {
    Jpeg j = baseJpeg();
    j.find(0xdb).payload[0] = 0x10;  // 16-bit table: 129 bytes needed, 65 present
    return j.bytes();
  });
  // Scan header.
  add("sos_bad_table", "malformed", "sos", "sos_ignored", "", [] {
    Jpeg j = baseJpeg();
    j.find(0xda).payload[2] = 0x44;
    return j.bytes();
  });
  add("sos_bad_component", "malformed", "sos", "sos_ignored", "", [] {
    Jpeg j = baseJpeg();
    j.find(0xda).payload[3] = 9;
    return j.bytes();
  });
  add("sos_bad_length", "malformed", "sos", "sos_ignored", "", [] {
    Jpeg j = baseJpeg();
    j.find(0xda).payload.push_back(0);
    return j.bytes();
  });
  // EXIF metadata: the bad block is skipped and the image still decodes.
  add("exif_ifd0_outside", "exif", "exif", "exif_outside", "valid_baseline_color", [] {
    Jpeg j = baseJpeg();
    Exif e;
    e.ifd0Offset = 0x7ffffff0;
    j.segments.insert(j.segments.begin() + 1, Segment{0xe1, exifPayload(e)});
    return j.bytes();
  });
  add("exif_ifd1_outside", "exif", "exif", "exif_outside", "valid_baseline_color", [] {
    Jpeg j = baseJpeg();
    Exif e;
    e.nextIfd = 0xffffff00;
    j.segments.insert(j.segments.begin() + 1, Segment{0xe1, exifPayload(e)});
    return j.bytes();
  });
  add("exif_ifd1_outside_intel", "exif", "exif", "exif_outside", "valid_baseline_color", [] {
    Jpeg j = baseJpeg();
    Exif e;
    e.mm = false;
    e.nextIfd = 0xffffff00;
    j.segments.insert(j.segments.begin() + 1, Segment{0xe1, exifPayload(e)});
    return j.bytes();
  });
  add("exif_tags_past_segment", "exif", "exif", "exif_outside", "valid_baseline_color", [] {
    // IFD0 claims 40 tags in a 40-byte APP1; the next segment holds bytes that parse as an orientation tag.
    Jpeg j = baseJpeg();
    Exif e;
    e.ifd0 = {{274, 3, 1, 6}};
    Bytes p = exifPayload(e);
    p[14] = 0, p[15] = 40;  // IFD0 tag count
    p.resize(40, 0);
    Bytes fake(8, 0);  // aligns the fake tags with IFD0's 12-byte slots
    for (int i = 0; i < 6; i++) putTag(fake, true, {274, 3, 1, 3});
    j.segments.insert(j.segments.begin() + 1, Segment{0xe1, p});
    j.segments.insert(j.segments.begin() + 2, Segment{0xfe, fake});
    return j.bytes();
  });
  add("exif_header_past_segment", "exif", "exif", "exif_outside", "valid_baseline_color", [] {
    Jpeg j = baseJpeg();
    j.segments.insert(j.segments.begin() + 1, Segment{0xe1, {'E', 'x', 'i', 'f', 0, 0, 'M', 'M', 0, 42}});
    return j.bytes();
  });
  add("exif_next_pointer_past_segment", "exif", "exif", "exif_outside", "valid_baseline_color", [] {
    Jpeg j = baseJpeg();
    Exif e;
    Bytes p = exifPayload(e);  // 6 + 8 + 2 + 12 + 4 bytes; drop the next-IFD pointer
    p.resize(p.size() - 4);
    j.segments.insert(j.segments.begin() + 1, Segment{0xe1, p});
    return j.bytes();
  });

  // Valid files: patched and unpatched decoders must agree and both must decode.
  add("valid_baseline_color", "valid", "", "", "", [] { return base(); });
  add("valid_baseline_gray", "valid", "", "", "", [] { return encode({.components = 1}).bytes(); });
  add("valid_combined_tables", "valid", "", "", "", [] { return encode({.combinedTables = true}).bytes(); });
  add("valid_word_quant", "valid", "", "", "", [] { return encode({.wordQuant = true}).bytes(); });
  add("valid_restart_interval", "valid", "", "", "", [] { return encode({.restartInterval = 1}).bytes(); });
  add("valid_progressive_dc", "valid", "", "", "", [] { return encode({.progressiveDc = true}).bytes(); });
  add("valid_large_app2", "valid", "", "", "", [] {
    Jpeg j = baseJpeg();
    j.segments.insert(j.segments.begin() + 1, Segment{0xe2, Bytes(60000, 0xa5)});
    return j.bytes();
  });
  add("valid_large_exif", "valid", "", "", "", [] {
    Jpeg j = baseJpeg();
    Exif e;
    e.ifd1 = kThumbTags;
    e.padTo = 40000;
    j.segments.insert(j.segments.begin() + 1, Segment{0xe1, exifPayload(e)});
    return j.bytes();
  });
  add("valid_exif_intel", "valid", "", "", "", [] {
    Jpeg j = baseJpeg();
    Exif e;
    e.mm = false;
    e.ifd1 = kThumbTags;
    j.segments.insert(j.segments.begin() + 1, Segment{0xe1, exifPayload(e)});
    return j.bytes();
  });
  add("valid_exif_ifd1_beyond_window", "valid", "", "", "", [] {
    Jpeg j = baseJpeg();
    Exif e;
    e.nextIfd = 3000;
    e.padTo = 5000;
    Bytes p = exifPayload(e);
    Bytes ifd1;
    put16(ifd1, true, uint32_t(kThumbTags.size()));
    for (const auto& t : kThumbTags) putTag(ifd1, true, t);
    std::copy(ifd1.begin(), ifd1.end(), p.begin() + 6 + 3000);
    j.segments.insert(j.segments.begin() + 1, Segment{0xe1, p});
    return j.bytes();
  });
  add("valid_exif_late_in_window", "valid", "", "", "", [] {
    // EXIF whose tags end just before the buffered window ends.
    Jpeg j = baseJpeg();
    Exif e;
    e.ifd1 = kThumbTags;
    e.padTo = 3000;
    j.segments.insert(j.segments.begin() + 1, Segment{0xe1, exifPayload(e)});
    placeAt(j, 1, 1000);
    return j.bytes();
  });
  add("valid_many_segments", "valid", "", "", "", [] {
    Jpeg j = baseJpeg();
    for (int i = 0; i < 120; i++) j.segments.insert(j.segments.begin() + 1, comment(size_t(37 + i * 7)));
    return j.bytes();
  });
  add("valid_trailing_data", "valid", "", "", "", [] {
    Jpeg j = baseJpeg();
    j.tail.insert(j.tail.end(), 300, 0xee);
    return j.bytes();
  });
  add("valid_app1_xmp", "valid", "", "", "", [] {
    Jpeg j = baseJpeg();
    Bytes p;
    const char* ns = "http://ns.adobe.com/xap/1.0/";
    p.insert(p.end(), ns, ns + std::strlen(ns) + 1);
    p.resize(900, ' ');
    j.segments.insert(j.segments.begin() + 1, Segment{0xe1, p});
    return j.bytes();
  });
  add("valid_tiny_app1", "valid", "", "", "", [] {
    Jpeg j = baseJpeg();
    j.segments.insert(j.segments.begin() + 1, Segment{0xe1, {'E', 'x'}});
    return j.bytes();
  });
  // Parsed segments around the 1,024-byte refill point (the parser moves the buffer there).
  for (int where : {1018, 1020, 1021, 1022, 1023, 1024, 1025, 1026}) {
    for (uint8_t marker : {uint8_t(0xdb), uint8_t(0xc0), uint8_t(0xc4), uint8_t(0xda)}) {
      add("valid_refill_" + std::to_string(marker) + "_" + std::to_string(where), "valid", "", "", "", [where, marker] {
        Jpeg j = encode({.restartInterval = marker == 0xda ? 2 : 0});
        placeAt(j, j.index(marker), size_t(where));
        return j.bytes();
      });
    }
  }
  // Unusual but tolerated layouts: the patched decoder must behave exactly like the unpatched one.
  add("unusual_fill_bytes", "unusual", "", "", "", [] {
    Jpeg j = baseJpeg();
    Bytes b = j.bytes();
    const size_t at = j.offsetOf(j.index(0xc0));
    b.insert(b.begin() + long(at), {0xff, 0xff});
    return b;
  });
  add("unusual_long_sof", "unusual", "", "", "", [] {
    Jpeg j = baseJpeg();
    j.find(0xc0).payload.insert(j.find(0xc0).payload.end(), {0, 0, 0});
    return j.bytes();
  });
  add("unusual_dht_trailing_bytes", "unusual", "", "", "", [] {
    Jpeg j = baseJpeg();
    Segment& s = j.find(0xc4);
    s.payload.insert(s.payload.end(), 10, 0);
    return j.bytes();
  });
  add("unusual_restart_marker_in_header", "unusual", "", "", "", [] {
    Jpeg j = baseJpeg();
    Bytes b = j.bytes();
    b.insert(b.begin() + long(j.offsetOf(j.index(0xc0))), {0xff, 0xd0});
    return b;
  });
  add("unusual_exif_bad_tag_count", "unusual", "", "", "", [] {
    Jpeg j = baseJpeg();
    Exif e;
    Bytes p = exifPayload(e);
    p[14] = 0x40;  // 16,385 tags: GetTIFFInfo ignores the IFD in both decoders
    j.segments.insert(j.segments.begin() + 1, Segment{0xe1, p});
    return j.bytes();
  });
  return c;
}

// ---------------------------------------------------------------- runner
struct MemFile {
  Bytes data;
  int32_t pos = 0;
};
int32_t memRead(JPEGFILE* f, uint8_t* buf, int32_t len) {
  auto* m = static_cast<MemFile*>(f->fHandle);
  const int32_t n = std::max<int32_t>(0, std::min<int32_t>(len, int32_t(m->data.size()) - m->pos));
  if (n) std::memcpy(buf, m->data.data() + m->pos, size_t(n));
  m->pos += n;
  f->iPos += n;
  return n;
}
int32_t memSeek(JPEGFILE* f, int32_t pos) {  // like SdFat: no seek past the end
  auto* m = static_cast<MemFile*>(f->fHandle);
  if (pos < 0 || pos > int32_t(m->data.size())) return -1;
  m->pos = pos;
  f->iPos = pos;
  return pos;
}
void memClose(void*) {}

uint64_t g_hash = 1469598103934665603ull;
void mix(uint64_t v) {
  g_hash ^= v;
  g_hash *= 1099511628211ull;
}
int draw(JPEGDRAW* d) {
  mix(uint64_t(d->x) << 32 | uint32_t(d->y));
  mix(uint64_t(d->iWidth) << 32 | uint32_t(d->iHeight));
  const auto* p = reinterpret_cast<const uint8_t*>(d->pPixels);
  for (int i = 0; i < d->iWidth * d->iHeight; i++) mix(p[i]);
  return 1;
}

int run(const std::string& label, Bytes data) {
  MemFile file{std::move(data)};
  const size_t size = file.data.size();
  auto* jpeg = new JPEGDEC;
  jhg_sos = 0;
  const int opened = jpeg->open(&file, int(size), memClose, memRead, memSeek, draw);
  int decoded = -1;
  if (opened == 1) {
    jpeg->setPixelType(EIGHT_BIT_GRAYSCALE);
    decoded = jpeg->decode(0, 0, 0);
  }
  std::printf(
      "{\"case\": \"%s\", \"size\": %zu, \"open\": %d, \"error\": %d, \"decode_error\": %d, \"width\": %d, "
      "\"height\": %d, \"orientation\": %d, \"thumb\": [%d, %d, %d], \"decode\": %d, \"hash\": \"%016llx\", "
      "\"hits\": {",
      label.c_str(), size, opened, opened == 1 ? 0 : jpeg->getLastError(), opened == 1 ? jpeg->getLastError() : 0,
      jpeg->getWidth(), jpeg->getHeight(), jpeg->getOrientation(), jpeg->hasThumb(), jpeg->getThumbWidth(),
      jpeg->getThumbHeight(), decoded, static_cast<unsigned long long>(decoded == 1 ? g_hash : 0));
  const char* sep = "";
  for (const auto& [site, n] : g_hits) {
    std::printf("%s\"%s\": %d", sep, site.c_str(), n);
    sep = ", ";
  }
  std::printf("}}\n");
  jpeg->close();
  delete jpeg;
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  const std::vector<Case> all = cases();
  const std::string mode = argc > 1 ? argv[1] : "";
  if (mode == "--list") {
    std::printf("[");
    for (size_t i = 0; i < all.size(); i++)
      std::printf(
          "%s{\"name\": \"%s\", \"kind\": \"%s\", \"guard\": \"%s\", \"control\": \"%s\", \"reference\": \"%s\"}",
          i ? ",\n " : "", all[i].name.c_str(), all[i].kind, all[i].guard, all[i].control, all[i].reference);
    std::printf("]\n");
    return 0;
  }
  if (mode == "--decode-error") {
    std::printf("%d\n", JPEG_DECODE_ERROR);
    return 0;
  }
  if (mode == "--file" && argc > 2) {
    std::ifstream in(argv[2], std::ios::binary);
    return run(argv[2], Bytes(std::istreambuf_iterator<char>(in), {}));
  }
  if ((mode == "--case" || mode == "--dump") && argc > 2) {
    for (const auto& c : all) {
      if (argv[2] != c.name) continue;
      if (mode == "--case") return run(c.name, c.make());
      if (argc < 4) return 2;
      const Bytes b = c.make();
      std::ofstream(argv[3], std::ios::binary).write(reinterpret_cast<const char*>(b.data()), long(b.size()));
      return 0;
    }
  }
  std::fprintf(stderr, "usage: %s --list | --decode-error | --case NAME | --dump NAME OUT | --file PATH\n", argv[0]);
  return 2;
}
