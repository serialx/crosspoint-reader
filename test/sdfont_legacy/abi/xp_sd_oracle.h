#pragma once
// Historical native SD compatibility ABI; never included by firmware.
#ifndef XP_APP_TEST
#error "SD oracle ABI is host-only"
#endif
// SD font v4 parsing. No allocation, I/O, callbacks or retained pointers.
// Byte spans are readable for len bytes (len <= PTRDIFF_MAX), NULL only if empty.
// Typed outputs/state are live, aligned and exclusive for the call. State is
// initialized by header (Directory) or {0, 0, glyphCount <= UINT16_MAX} (Intervals).
// Input/output/state do not overlap, except interval output may alias its bytes.
// False leaves outputs/state unchanged. Malformed bytes never panic.
// Offsets retain uint32 wrap; table I/O and bitmap extent checks remain deferred.

// Synchronous, exclusive cache loans. Slots retain their addresses until unload.
// Resident buffers use the paired PSRAM allocator; scratch uses ordinary arrays.
// File callbacks are nested at most twice and may not reenter cache operations.
// close(false) closes the file; close(true) also releases its HAL handle.

// Bounded nonterminated visual text; same pool loan and allocation policy.

// Host ABI test: field offsets and sizes of the stable native cache slots.

// Detail is writable, aligned, and disjoint from input, state and output.

// out points to a writable EpdUnicodeInterval (three uint32 fields).

// A class ID outside count is returned as zero. Invalid input leaves cp unchanged.

// SD loader callbacks borrow the current HAL file. Style records and the
// sixteen shared-interval entries retain their existing C++ storage and size.

// get returns a plain record for index < count. The owner and borrowed filenames
// must remain live and unchanged throughout the synchronous operation.
#ifdef __cplusplus
extern "C" {
#endif
// Discovery callbacks borrow names for one call. The facade owns directory
// handles and C++ containers; Rust retains no pointers after the operation.
#ifdef __cplusplus
enum class XpFontDiscoveryEvent : uint32_t { MissingRoot = 1, NotDirectory = 2, Duplicate = 3, Family = 4, Vector = 5 };
#endif

// The manager owns fonts returned by create: failed loads call destroy;
// successful loads transfer ownership into the existing loaded-font container.
// Names and views are synchronous loans that expire on container mutation.
#ifdef __cplusplus
enum class XpFontLoadMode : uint8_t { Replace, ExtraSize, Variant };
enum class XpFontManagerEvent : uint32_t {
  Path = 1,
  Allocation = 2,
  Load = 3,
  Collision = 4,
  Loaded = 5,
  EmptyFamily = 6
};
#endif
uint32_t xp_font_sd_glyph(const XpFontSdView* view, uint32_t cp, bool metrics, XpFontGlyph* out);
bool xp_font_sd_has_codepoint(const XpFontSdView* view, uint32_t cp);
bool xp_font_sd_advance(const XpFontSdView* view, uint32_t cp, uint16_t* out);
typedef struct {
  uint32_t intervalCount, glyphCount;
  uint8_t advanceY;
  int16_t ascender, descender;
  bool is2Bit;
  uint16_t kernLeftEntryCount, kernRightEntryCount;
  uint8_t kernLeftClassCount, kernRightClassCount, ligaturePairCount;
} XpSdfontHeader;
typedef struct {
  uint32_t hash;
  uint8_t styleCount;
  bool is2Bit;
  uint8_t seen;
} XpSdfontDirectory;
typedef struct {
  XpSdfontHeader header;
  uint32_t offsets[7];  // intervals, glyphs, left/right kern, matrix, ligatures, bitmap
  uint8_t id;
} XpSdfontStyle;
typedef struct {
  uint32_t expectedOffset, previousLast;
  bool bmp16;
} XpSdfontIntervals;
typedef struct {
  void* context;
  bool (*open)(void*);
  void (*close)(void*, bool release);
  bool (*read)(void*, uint32_t offset, uint8_t* destination, size_t length, bool seek);
  uint8_t* (*alloc)(size_t bytes, bool resident);
  void (*free)(uint8_t*, bool resident);
  uint32_t (*heap)(bool largest);
  uint32_t (*clock)(void);
  void (*corrupt)(void*, const void* glyph, uint32_t codepoint);
  // Optional; synchronous and non-reentrant. Integer arguments are event-specific.
  void (*diagnostic)(void*, uint32_t event, uint32_t a, uint32_t b, uint32_t c, uint32_t d);
} XpSdfontServices;
typedef struct {
  void* styles;
  const uint8_t* slots;
  void** advances;
  uint32_t* advanceSizes;
  uint32_t* advanceCapacities;
  void* overflow;
  uint32_t* overflowCount;
  uint32_t* overflowNext;
  void* stats;
  uint32_t fileSize;
  bool loaded;
  const XpSdfontServices* services;
} XpSdfontPool;
int32_t xp_sdfont_pool_prewarm(XpSdfontPool*, const char* (*getter)(const void*, uint32_t), const void*, uint32_t,
                               uint8_t, bool metadata, bool kern, bool accumulate);
int32_t xp_sdfont_pool_prewarm_span(XpSdfontPool*, const uint8_t*, size_t, uint8_t mask, bool metadata, bool kern,
                                    bool accumulate);
int32_t xp_sdfont_pool_advances(XpSdfontPool*, const XpSdfontTexts*, uint8_t mask);
bool xp_sdfont_pool_lookup(const XpSdfontPool*, uint32_t cp, uint8_t style, uint16_t* outAdvance);
uint16_t xp_sdfont_pool_advance(const XpSdfontPool*, uint32_t cp, uint8_t style);
uint8_t xp_sdfont_pool_resolve(const XpSdfontPool*, uint8_t style);
uint8_t xp_sdfont_pool_mask(const XpSdfontPool*, uint8_t mask);
bool xp_sdfont_pool_coverage(const XpSdfontPool*, uint32_t cp, uint8_t style);
const void* xp_sdfont_pool_miss(XpSdfontPool*, uint32_t cp, uint8_t style);
void xp_sdfont_pool_clear_advances(XpSdfontPool*);
void xp_sdfont_pool_clear_cache(XpSdfontPool*);
size_t xp_sdfont_pool_layout(uint32_t field);
bool xp_sdfont_header_diagnostic(const uint8_t*, size_t, XpSdfontDirectory*, XpSdfontParseDiagnostic*);
bool xp_sdfont_style_diagnostic(const uint8_t*, size_t, XpSdfontDirectory*, XpSdfontStyle*, XpSdfontParseDiagnostic*);
bool xp_sdfont_interval_diagnostic(const uint8_t*, size_t, uint32_t glyphs, uint32_t index, XpSdfontIntervals*,
                                   void* out, XpSdfontParseDiagnostic*);
bool xp_sdfont_header(const uint8_t*, size_t, XpSdfontDirectory*);
bool xp_sdfont_style(const uint8_t*, size_t, XpSdfontDirectory*, XpSdfontStyle*);
bool xp_sdfont_interval(const uint8_t*, size_t, uint32_t glyphs, uint32_t index, XpSdfontIntervals*, void* out);
bool xp_sdfont_glyph_valid(const uint8_t*, size_t, bool twoBit, uint32_t bitmapOffset, uint32_t fileSize);
uint8_t xp_sdfont_kern_class(const uint8_t*, size_t, uint8_t count, uint32_t* cp);
bool xp_sdfont_ligature(const uint8_t*, size_t, uint32_t* left, uint32_t* right, uint32_t* out);
bool xp_sdfont_filename(const uint8_t*, size_t, uint8_t* size);
typedef struct {
  const void* table;
  uint32_t count;
  bool bmp16;
  uint16_t refs;
} XpSdfontShared;
typedef struct {
  void* ctx;
  int32_t (*read)(void*, uint32_t offset, uint8_t* dst, size_t length, bool seek);
  void* (*allocStyles)(void*, uint8_t count);
  uint8_t* (*alloc)(size_t);
  void (*cleanup)(void*);
  void (*setup)(void*, uint8_t);
  void (*diagnostic)(void*, uint32_t, uint32_t, uint32_t, uint32_t);
  void (*parseError)(const XpSdfontParseDiagnostic*, uint32_t, uint32_t);
} XpSdfontLoadServices;
bool xp_sdfont_load(uint8_t* slots, uint32_t* hash, XpSdfontShared*, const XpSdfontLoadServices*);
void xp_sdfont_clear_loaded(XpSdfontPool*, XpSdfontShared*, uint8_t count, bool all, void* ctx,
                            void (*setup)(void*, uint8_t), void (*generation)());
typedef struct {
  const char* name;
  uint8_t size, style;
} XpFontFile;
typedef struct XpFontFiles {
  const void* ctx;
  size_t count;
  XpFontFile (*get)(const void*, size_t index);
} XpFontFiles;
typedef struct {
  size_t index;
  uint16_t weight;
  bool italic;
} XpFontCandidate;
int32_t xp_font_file_nearest(const XpFontFiles*, uint8_t size, uint8_t style, bool exact);
bool xp_font_file_path(const char* root, const char* family, const char* file, uint8_t size, bool loose, uint8_t* out,
                       size_t capacity);
bool xp_font_select_faces(const XpFontCandidate*, size_t, const XpFontFiles*, int32_t* selected);
typedef struct {
  void* ctx;
  bool vectors;
  int32_t (*open)(void*, const char*, bool root);
  bool (*next)(void*, bool root, uint8_t* name, bool* directory);
  void (*close)(void*, bool root);
  size_t (*families)(void*);
  const char* (*familyName)(void*, size_t);
  void (*beginFamily)(void*, const char* root, const uint8_t* name, size_t length, bool loose);
  XpFontFiles (*files)(void*, bool vector);
  bool (*addFile)(void*, bool vector, const char* name, uint8_t size, uint8_t style, bool canonical);
  size_t (*commitFamily)(void*, bool vector, const char* directory);
  void (*diagnostic)(void*, uint32_t, const char*, const char*, size_t);
  void (*endFamily)(void*);
} XpFontDiscovery;
void xp_font_scan_root(const XpFontDiscovery*, const char* root);
void xp_font_sizes(const XpFontFiles*, void*, void (*emit)(void*, uint8_t));
int32_t xp_font_find_family(const void*, size_t, const char* (*get)(const void*, size_t), const uint8_t*, size_t);
const char* xp_font_root(const char* family, const char* hidden, const char* visible, bool (*exists)(const char*),
                         bool write);
bool xp_font_has_size(const XpFontFiles*, uint8_t);
typedef struct {
  void* ctx;
  void (*clear)(void*);
  void (*scan)(void*, const char*);
  size_t (*sort)(void*);
  void (*resize)(void*, size_t);
  size_t (*finish)(void*);
} XpFontRegistry;
bool xp_font_discover(const XpFontRegistry*, const char* hidden, const char* visible, size_t maximum);
typedef struct {
  void* font;
  int32_t id;
  uint8_t size;
} XpFontLoaded;
typedef struct {
  const char* name;
  size_t nameLen;
  const XpFontLoaded* entries;
  size_t count;
} XpFontManagerView;
typedef struct {
  const char* name;
  size_t nameLen;
  const char* root;
  XpFontFiles files;
  bool loose;
} XpFontFamilyFiles;
typedef struct {
  void* ctx;
  XpFontManagerView (*view)(void*);
  void* (*create)();
  bool (*load)(void*, const char*);
  void (*destroy)(void*);
  uint32_t (*info)(void*, uint8_t* styles);
  bool (*collision)(void*, int32_t);
  void (*registerSd)(void*, int32_t, void*);
  void (*push)(void*, XpFontLoaded);
  void (*insertFont)(void*, int32_t, void*);
  void (*removeFont)(void*, int32_t);
  void (*erase)(void*, size_t);
  void (*clearFallbacks)(void*);
  void (*clearSd)(void*);
  void (*clear)(void*);
  void (*family)(void*, const char*, size_t, uint8_t);
  void (*diagnostic)(void*, uint32_t, const char*, int32_t, uint32_t, uint32_t);
} XpFontManagerServices;
void xp_font_manager_destroy(const XpFontManagerServices*);
void xp_font_manager_unload(const XpFontManagerServices*, int32_t id, bool all);
int32_t xp_font_manager_get(const XpFontManagerServices*, const char* family, size_t nameLen, uint8_t size);
int32_t xp_font_manager_load(const XpFontManagerServices*, const XpFontFamilyFiles*, uint8_t size, int32_t id,
                             uint8_t mode);
bool xp_font_canonical(const char* name, const uint8_t* family, size_t length, uint8_t size);
#ifdef __cplusplus
}
#endif
