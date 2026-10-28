#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  XP_JSONQUERY_NULL = 0,
  XP_JSONQUERY_ARRAYSIZE = 1,
  XP_JSONQUERY_U8 = 2,
  XP_JSONQUERY_U16 = 3,
  XP_JSONQUERY_U32 = 4,
  XP_JSONQUERY_USIZE = 5,
  XP_JSONQUERY_ISUSIZE = 6,
  XP_JSONQUERY_ISU32 = 7,
  XP_JSONQUERY_ISINT = 8,
  XP_JSONQUERY_INT = 9,
  XP_JSONQUERY_ISU8 = 10,
  XP_JSONQUERY_BOOL = 11,
  XP_JSONQUERY_ISSTRING = 12,
  XP_JSONQUERY_ISU16 = 13,
} XpStoreJsonQuery;
typedef enum {
  XP_JSONWRITE_ARRAY = 0,
  XP_JSONWRITE_NUMBER = 1,
  XP_JSONWRITE_BOOL = 2,
  XP_JSONWRITE_STRING = 3,
  XP_JSONWRITE_BORROWEDSTRING = 4,
} XpStoreJsonWrite;
typedef enum {
  XP_ROWEDIT_CLEAR = 0,
  XP_ROWEDIT_RESERVE = 1,
  XP_ROWEDIT_RESETSCRATCH = 2,
  XP_ROWEDIT_APPENDSCRATCH = 3,
  XP_ROWEDIT_ERASE = 4,
  XP_ROWEDIT_MOVETOFRONT = 5,
  XP_ROWEDIT_INSERTFRONT = 7,
  XP_ROWEDIT_RESIZE = 8,
  XP_ROWEDIT_APPENDMOVE = 10,
} XpStoreRowEdit;
typedef enum {
  XP_STOREKIND_RECENT = 0,
  XP_STOREKIND_WIFI = 1,
  XP_STOREKIND_OPDS = 2,
  XP_STOREKIND_TOKENS = 3,
} XpStoreStoreKind;
typedef enum {
  XP_ROWEVENT_INVALIDLENGTH = 1,
  XP_ROWEVENT_OVERSIZELENGTH = 2,
  XP_ROWEVENT_OVERSIZEPASSWORD = 3,
  XP_ROWEVENT_LENGTHMISMATCH = 4,
  XP_ROWEVENT_CHECKSUMMISMATCH = 5,
  XP_ROWEVENT_INVALIDCHECKSUM = 6,
  XP_ROWEVENT_LOADED = 7,
  XP_ROWEVENT_RESAVE = 8,
} XpStoreRowEvent;
typedef enum {
  XP_PERSISTLEAF_EXISTS = 0,
  XP_PERSISTLEAF_READ = 1,
  XP_PERSISTLEAF_PARSE = 2,
  XP_PERSISTLEAF_MKDIR = 3,
  XP_PERSISTLEAF_SERIALIZE = 4,
  XP_PERSISTLEAF_WRITE = 5,
} XpStorePersistLeaf;
typedef enum {
  XP_MUTATION_RECENTADD = 0,
  XP_MUTATION_RECENTUPDATE = 1,
  XP_MUTATION_REMOVE = 2,
  XP_MUTATION_WIFIUPSERT = 3,
  XP_MUTATION_TOKENPUT = 4,
  XP_MUTATION_WIFIREMOVE = 5,
} XpStoreMutation;
typedef enum {
  XP_MUTATIONRESULT_REJECTED = 0,
  XP_MUTATIONRESULT_UNCHANGED = 1,
  XP_MUTATIONRESULT_UPDATED = 2,
  XP_MUTATIONRESULT_INSERTED = 3,
  XP_MUTATIONRESULT_REMOVED = 4,
} XpStoreMutationResult;

// Borrowed synchronous leaves; callbacks may not retain pointers. Diagnostic callbacks must not reenter Rust.
typedef struct {
  void* ctx;
  int32_t (*read)(void*, uint8_t*, size_t);
  size_t (*write)(void*, const uint8_t*, size_t);
  bool (*seek)(void*, size_t);
} XpStoreFileOps;
typedef struct {
  size_t fill, pos;
  bool ok;
} XpStoreWriter;
typedef struct {
  size_t fill, off, start;
} XpStoreReader;
void xp_store_buffer_write(XpStoreWriter*, uint8_t*, size_t, const XpStoreFileOps*, const uint8_t*, size_t);
bool xp_store_buffer_flush(XpStoreWriter*, const uint8_t*, const XpStoreFileOps*);
size_t xp_store_buffer_read(XpStoreReader*, uint8_t*, size_t, const XpStoreFileOps*, uint8_t*, size_t);
bool xp_store_buffer_seek(XpStoreReader*, size_t, const XpStoreFileOps*, size_t);
void xp_store_xor(uint8_t*, size_t, const uint8_t*, size_t);
void xp_store_recent_push(uint16_t*, uint8_t*, uint8_t*, uint16_t);
bool xp_store_recent_contains(const uint16_t*, uint8_t, uint8_t, uint16_t, uint8_t);
typedef struct {
  const uint8_t* data;
  size_t len;
} XpStoreBytes;
typedef struct {
  const char* key;
  size_t index;
  const char* field;
  size_t subindex;
  const char* subfield;
  bool static_keys;
} XpStoreJsonPath;
typedef struct {
  void* ctx;
  uint64_t (*number)(void*, const XpStoreJsonPath*, uint32_t, uint64_t);
  XpStoreBytes (*text)(void*, const XpStoreJsonPath*);
  void (*put)(void*, const XpStoreJsonPath*, uint32_t, uint64_t, XpStoreBytes);
} XpStoreJsonOps;
typedef struct {
  void* ctx;
  size_t (*count)(void*);
  void (*edit)(void*, uint32_t, size_t);
  XpStoreBytes (*get)(void*, size_t, uint32_t);
  void (*set)(void*, size_t, uint32_t, XpStoreBytes);
  uint32_t (*decode)(void*, uint32_t, XpStoreBytes, size_t);
  void (*emit)(void*, size_t, uint32_t, const XpStoreJsonOps*, const XpStoreJsonPath*, bool);
  void (*diagnostic)(void*, uint32_t, size_t, size_t, size_t);
} XpStoreRows;
bool xp_store_rows_load(uint32_t, const XpStoreJsonOps*, const XpStoreRows*);
void xp_store_rows_save(uint32_t, const XpStoreJsonOps*, const XpStoreRows*);
typedef struct {
  void* ctx;
  XpStoreBytes (*path_get)(void*);
  void (*path_set)(void*, XpStoreBytes);
  uint16_t* images;
  uint8_t *pos, *fill;
  uint16_t* overlay;
  uint8_t *overlay_pos, *overlay_fill, *load_count;
  bool *from_reader, *boot;
} XpStoreState;
void xp_store_state_load(const XpStoreState*, const XpStoreJsonOps*);
void xp_store_state_save(const XpStoreState*, const XpStoreJsonOps*);
typedef struct {
  void* ctx;
  bool (*call)(void*, uint32_t);
} XpStorePersistOps;
uint32_t xp_store_document(const XpStorePersistOps*, bool);
uint32_t xp_store_password_action(bool, bool, size_t, size_t);
bool xp_store_load(void*, uint32_t (*)(void*), bool (*)(void*), void (*)(void*));
typedef struct {
  void* ctx;
  const uint8_t* (*key)();
  uint8_t* (*resize)(void*, size_t);
  int32_t (*decode)(uint8_t*, size_t, size_t*, const uint8_t*, size_t);
} XpStoreDecodeOps;
uint32_t xp_store_decode(const uint8_t*, size_t, size_t, const XpStoreDecodeOps*, int32_t*);
typedef struct {
  const char* key;
  uint8_t *value, *text;
  size_t capacity;
  uint32_t kind;
  uint8_t min, max;
  bool obfuscated;
} XpSettingField;
typedef struct {
  void* ctx;
  XpStoreBytes (*decode)(void*, XpStoreBytes, size_t, uint32_t*);
  XpStoreBytes (*encode)(void*, const uint8_t*);
  void (*diagnostic)(void*, uint32_t, const char*);
} XpSettingCodec;
bool xp_setting_field(const XpSettingField*, const XpStoreJsonOps*, const XpSettingCodec*, bool);
typedef struct {
  uint8_t* front[4];
  uint8_t *font_family, *font_size, *long_press, *short_power, *language;
  uint16_t* layouts;
  uint8_t *plugins, *indent, *extra_spacing, *touch, *next, *previous, *sleep, *home;
  uint8_t* sd_font;
  size_t sd_capacity;
  uint8_t* dictionary;
  size_t dictionary_capacity;
  bool (*plugins_installed)();
  XpStoreBytes (*language_code)(uint8_t);
  uint8_t (*language_from_code)(const uint8_t*);
  bool has_home;
  uint8_t long_press_count, short_power_count;
} XpStoreSettings;
bool xp_settings_tail(const XpStoreSettings*, const XpStoreJsonOps*, bool);
void xp_setting_front(uint8_t* const*);
uint8_t xp_setting_sleep_minutes(uint8_t);
float xp_setting_line_compression(bool, uint8_t);
uint32_t xp_setting_sleep_ms(uint8_t);
int32_t xp_setting_refresh(uint8_t);
size_t xp_store_find(const XpStoreRows*, uint32_t, XpStoreBytes);
uint32_t xp_store_mutate(const XpStoreRows*, uint32_t, const XpStoreBytes*);
bool xp_store_prune(const XpStoreRows*, bool (*)(void*, size_t), void (*)(void*, size_t, size_t));
bool xp_store_prefix(XpStoreBytes, XpStoreBytes);
bool xp_store_index_action(size_t, size_t, bool);
bool xp_store_last_connected(const XpStoreRows*, XpStoreBytes);
bool xp_store_string_length(uint32_t, size_t, size_t, int32_t);
#ifdef __cplusplus
}
#endif
