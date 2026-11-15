#pragma once
// Face sources are borrowed and must outlive the family. Each service call
// lends existing vector storage; buffer pointers expire after reserve/resize.
// No callback may reenter this face cache; eviction locks its owner against release.
#ifdef __cplusplus
enum class XpVectorHeapMetric : uint8_t { LargestPsram, LargestInternal, FreeInternal };
enum class XpVectorBufferId : uint8_t { Bitmap, Glyphs, Codepoints, Slots, KernKeys, KernValues };
#endif
typedef struct {
  uint32_t pair, ligatureCp;
} XpVectorLigature;
typedef struct {
  size_t used, cap;
  uint16_t maxGlyphs;
  bool twoBit;
  uint16_t sizePx;
  uint8_t srcIndex;
  int32_t weight;
  bool wantItalic, inited, ready;
  uint32_t ligGid[5];
  XpVectorLigature ligPairs[5];
  uint8_t ligPairCount;
} XpVectorState;
typedef struct {
  uint8_t* data;
  size_t length, capacity;
} XpVectorBuffer;
typedef struct {
  const uint8_t* pixels;
  uint16_t width, height;
  int16_t left, yoff;
  int32_t advance, metrics;
  bool haveMetrics;
} XpVectorRaster;
typedef struct {
  bool present, streamed;
  const uint8_t* data;
  uint32_t len;
  unsigned long (*read)(void*, unsigned long, unsigned char*, unsigned long);
  void* ctx;
  unsigned long fileSize;
} XpVectorSource;
typedef struct {
  void* ctx;
  XpVectorState* (*state)(void*);
  void (*init)(void*);
  XpVectorBuffer (*buffer)(void*, uint8_t);
  void (*reserve)(void*, uint8_t, size_t);
  void (*resize)(void*, uint8_t, size_t);
  void (*insert)(void*, uint8_t, size_t, const uint8_t*);
  void (*release)(void*, uint8_t);
  size_t (*heap)(uint8_t);
  void (*evict)(void*);
  uint32_t (*glyphId)(void*, uint32_t);
  void (*raster)(void*, uint32_t, XpVectorRaster*);
  int32_t (*kern)(void*, uint32_t, uint32_t);
  bool (*ftInit)(void*, const XpVectorSource*, uint16_t, int32_t, bool);
  void (*options)(void*);
  void (*gposBudget)(void*, size_t);
  void (*gsubBudget)(void*, size_t);
  uint32_t (*ligature)(void*, const uint32_t*, size_t);
  void (*releaseGsub)(void*);
  bool (*hasGlyph)(void*, uint32_t);
  void (*deinit)(void*);
  void (*lineMetrics)(void*, bool, int32_t*, int32_t*);
} XpVectorServices;
const void* xp_vector_glyph(const XpVectorServices*, uint32_t);
int8_t xp_vector_kern(const XpVectorServices*, uint32_t, uint32_t);
typedef struct {
  void* ctx;
  const XpVectorSource* sources;
  uint32_t* size;
  bool* loaded;
  const bool* locked;
  XpVectorServices (*face)(void*, uint8_t);
  void (*setup)(void*);
} XpVectorFamily;
void xp_vector_init(const XpVectorServices*, const XpVectorSource*);
bool xp_vector_load(const XpVectorFamily*, uint16_t pointSize, bool twoBit, size_t cap, uint16_t maximum);
void xp_vector_clear(const XpVectorFamily*, bool resident);
bool xp_vector_prewarm(const XpVectorFamily*, const uint8_t*, size_t length, bool reset);
bool xp_vector_coverage(const XpVectorServices*, uint32_t);
void xp_vector_metrics(const XpVectorServices*, void* fontData);
