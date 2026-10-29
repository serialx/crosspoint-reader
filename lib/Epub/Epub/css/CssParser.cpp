#include "CssParser.h"

#include <Logging.h>
#include <Memory.h>
#include <xp_book.h>
#include <xp_ffi.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>

namespace {
void cssDiagnostic(void*, uint32_t event, uint32_t a, uint32_t b, uint32_t, uint32_t) {
  switch (static_cast<XpCssDiagnosticEvent>(event)) {
    case XP_CSS_INDEX_OOM:
      LOG_ERR("CSS", "OOM: selector index (%zu entries)", static_cast<size_t>(a));
      break;
    case XP_CSS_SELECTOR_OOM:
      LOG_ERR("CSS", "OOM: selector pool (%zu bytes)", static_cast<size_t>(a));
      break;
    case XP_CSS_STYLE_OOM:
      LOG_ERR("CSS", "OOM: style pool (%zu styles)", static_cast<size_t>(a));
      break;
    case XP_CSS_SELECTOR_LONG:
      LOG_DBG("CSS", "Selector too long (%zu > %zu), skipping", static_cast<size_t>(a), static_cast<size_t>(b));
      break;
    case XP_CSS_STORE_LIMIT:
      LOG_ERR("CSS", "CSS rule store limit reached at %u rules", a);
      break;
    case XP_CSS_STORE_OOM:
      LOG_ERR("CSS", "OOM while growing CSS rule store at %u rules", a);
      break;
    case XP_CSS_INPUT_TRUNCATED:
      LOG_ERR("CSS", "CSS input exceeded parser buffer; cache will remain partial");
      break;
    case XP_CSS_CACHE_FLAGS:
      LOG_DBG("CSS", "Invalid CSS cache flags: %u", a);
      break;
    case XP_CSS_CACHE_COUNT:
      LOG_DBG("CSS", "Invalid cache rule count (%u > %zu)", a, static_cast<size_t>(b));
      break;
    case XP_CSS_CACHE_SELECTOR_LENGTH:
      LOG_DBG("CSS", "Invalid selector length in cache: %u", a);
      break;
    case XP_CSS_CACHE_DUPLICATE:
      LOG_DBG("CSS", "Duplicate selector in CSS cache");
      break;
    case XP_CSS_SCRATCH_OOM:
      LOG_ERR("CSS", "OOM: CSS parser scratch");
      break;
    case XP_CSS_CACHE_SCRATCH_OOM:
      LOG_ERR("CSS", "Cache load failed (%ld)", static_cast<long>(a));
      break;
  }
}
constexpr XpCssDiagnostics CSS_DIAGNOSTICS = {nullptr, cssDiagnostic};
int32_t readCssCache(XpCss* handle, const uint8_t* data, size_t len, bool end) {
  XpCssCacheDiagnostic detail{};
  const auto status = xp_css_cache_read_with_diagnostic(handle, data, len, end, &detail);
  if (detail.event != 0) cssDiagnostic(nullptr, detail.event, detail.a, detail.b, 0, 0);
  return status;
}
constexpr size_t READ_BUFFER_SIZE = 512;
constexpr std::array STYLE_LENGTH_FIELDS = {
    &CssStyle::textIndent,   &CssStyle::marginTop,   &CssStyle::marginBottom,  &CssStyle::marginLeft,
    &CssStyle::marginRight,  &CssStyle::paddingTop,  &CssStyle::paddingBottom, &CssStyle::paddingLeft,
    &CssStyle::paddingRight, &CssStyle::imageHeight, &CssStyle::imageWidth,    &CssStyle::fontSize,
};
constexpr size_t STYLE_LENGTH_FIELD_COUNT = STYLE_LENGTH_FIELDS.size();
constexpr size_t STYLE_WIRE_BYTES =
    5 + STYLE_LENGTH_FIELD_COUNT * (sizeof(decltype(CssLength::value)) + 1) + 6 + 13 + sizeof(uint32_t);
constexpr uint32_t CSS_DEFINED_BITS_MASK = (1u << 31) - 1;
constexpr uint8_t MAX_INITIAL_LETTER = 6;

bool decodeStyleWire(const uint8_t (&in)[STYLE_WIRE_BYTES], CssStyle& style) {
  size_t offset = 0;
  const uint8_t textAlign = in[offset++];
  const uint8_t fontStyle = in[offset++];
  const uint8_t fontWeight = in[offset++];
  const uint8_t textDecoration = in[offset++];
  const uint8_t direction = in[offset++];
  if (textAlign > static_cast<uint8_t>(CssTextAlign::None) || fontStyle > static_cast<uint8_t>(CssFontStyle::Inherit) ||
      fontWeight > static_cast<uint8_t>(CssFontWeight::Inherit) || (textDecoration & ~CSS_TEXT_DECORATION_MASK) != 0 ||
      direction > static_cast<uint8_t>(CssTextDirection::Rtl)) {
    return false;
  }
  style.textAlign = static_cast<CssTextAlign>(textAlign);
  style.fontStyle = static_cast<CssFontStyle>(fontStyle);
  style.fontWeight = static_cast<CssFontWeight>(fontWeight);
  style.textDecoration = static_cast<CssTextDecoration>(textDecoration);
  style.direction = static_cast<CssTextDirection>(direction);

  const auto getLength = [&in, &offset](CssLength& length) {
    decltype(CssLength::value) value = 0;
    memcpy(&value, in + offset, sizeof(value));
    offset += sizeof(length.value);
    const uint8_t unit = in[offset++];
    if (!std::isfinite(value) || unit > static_cast<uint8_t>(CssUnit::Percent)) return false;
    length.value = value;
    length.unit = static_cast<CssUnit>(unit);
    return true;
  };
  for (const auto field : STYLE_LENGTH_FIELDS) {
    if (!getLength(style.*field)) return false;
  }

  const uint8_t display = in[offset++];
  const uint8_t verticalAlign = in[offset++];
  const uint8_t listStyleType = in[offset++];
  if (display > static_cast<uint8_t>(CssDisplay::None) || verticalAlign > static_cast<uint8_t>(CssVerticalAlign::Sub) ||
      listStyleType > static_cast<uint8_t>(CssListStyleType::None)) {
    return false;
  }
  style.display = static_cast<CssDisplay>(display);
  style.verticalAlign = static_cast<CssVerticalAlign>(verticalAlign);
  style.listStyleType = static_cast<CssListStyleType>(listStyleType);

  const uint8_t smallCaps = in[offset++];
  const uint8_t pageBreakBefore = in[offset++];
  const uint8_t pageBreakAfter = in[offset++];
  if (smallCaps > 1 || pageBreakBefore > 1 || pageBreakAfter > 1) return false;
  style.smallCaps = smallCaps != 0;
  style.pageBreakBefore = pageBreakBefore != 0;
  style.pageBreakAfter = pageBreakAfter != 0;

  for (CssBorderSide* side : {&style.borderTop, &style.borderRight, &style.borderBottom, &style.borderLeft}) {
    side->width = in[offset++];
    const uint8_t borderStyle = in[offset++];
    if (borderStyle > static_cast<uint8_t>(CssBorderStyle::Dashed)) return false;
    side->style = static_cast<CssBorderStyle>(borderStyle);
  }
  const uint8_t borderComponents = in[offset++];
  style.borderWidthsDefined = borderComponents & 0x0f;
  style.borderStylesDefined = borderComponents >> 4;
  const uint8_t shaded = in[offset++];
  const uint8_t floatLeft = in[offset++];
  const uint8_t initialLetter = in[offset++];
  if (shaded > 1 || floatLeft > 1 || initialLetter > MAX_INITIAL_LETTER) return false;
  style.shaded = shaded != 0;
  style.floatLeft = floatLeft != 0;
  style.initialLetter = initialLetter;
  const uint8_t preserveWhitespace = in[offset++];
  if (preserveWhitespace > 1) return false;
  style.preserveWhitespace = preserveWhitespace != 0;

  uint32_t definedBits = 0;
  memcpy(&definedBits, in + offset, sizeof(definedBits));
  if ((definedBits & ~CSS_DEFINED_BITS_MASK) != 0) return false;
  style.defined.textAlign = (definedBits & 1 << 0) != 0;
  style.defined.fontStyle = (definedBits & 1 << 1) != 0;
  style.defined.fontWeight = (definedBits & 1 << 2) != 0;
  style.defined.textDecoration = (definedBits & 1 << 3) != 0;
  style.defined.textIndent = (definedBits & 1 << 4) != 0;
  style.defined.marginTop = (definedBits & 1 << 5) != 0;
  style.defined.marginBottom = (definedBits & 1 << 6) != 0;
  style.defined.marginLeft = (definedBits & 1 << 7) != 0;
  style.defined.marginRight = (definedBits & 1 << 8) != 0;
  style.defined.paddingTop = (definedBits & 1 << 9) != 0;
  style.defined.paddingBottom = (definedBits & 1 << 10) != 0;
  style.defined.paddingLeft = (definedBits & 1 << 11) != 0;
  style.defined.paddingRight = (definedBits & 1 << 12) != 0;
  style.defined.imageHeight = (definedBits & 1 << 13) != 0;
  style.defined.imageWidth = (definedBits & 1 << 14) != 0;
  style.defined.display = (definedBits & 1 << 15) != 0;
  style.defined.direction = (definedBits & 1 << 16) != 0;
  style.defined.verticalAlign = (definedBits & 1 << 17) != 0;
  style.defined.listStyleType = (definedBits & 1 << 18) != 0;
  style.defined.fontSize = (definedBits & 1 << 19) != 0;
  style.defined.smallCaps = (definedBits & 1 << 20) != 0;
  style.defined.pageBreakBefore = (definedBits & 1 << 21) != 0;
  style.defined.pageBreakAfter = (definedBits & 1 << 22) != 0;
  style.defined.borderTop = (definedBits & 1 << 23) != 0;
  style.defined.borderRight = (definedBits & 1 << 24) != 0;
  style.defined.borderBottom = (definedBits & 1 << 25) != 0;
  style.defined.borderLeft = (definedBits & 1 << 26) != 0;
  style.defined.shaded = (definedBits & 1 << 27) != 0;
  style.defined.floatLeft = (definedBits & 1 << 28) != 0;
  style.defined.initialLetter = (definedBits & 1 << 29) != 0;
  style.defined.whiteSpace = (definedBits & 1u << 30) != 0;
  return true;
}

static_assert(STYLE_WIRE_BYTES == XP_CSS_WIRE_BYTES);
static_assert(sizeof(CssAncestor) == sizeof(XpCssAncestor));
static_assert(alignof(CssAncestor) == alignof(XpCssAncestor));
static_assert(offsetof(CssAncestor, tagHash) == offsetof(XpCssAncestor, tag_hash));
static_assert(offsetof(CssAncestor, idHash) == offsetof(XpCssAncestor, id_hash));
static_assert(offsetof(CssAncestor, classHashes) == offsetof(XpCssAncestor, class_hashes));
static_assert(offsetof(CssAncestor, classCount) == offsetof(XpCssAncestor, class_count));
XpCssSlice slice(std::string_view view) { return {reinterpret_cast<const uint8_t*>(view.data()), view.size()}; }
}  // namespace

CssParser::~CssParser() { clear(); }

bool CssParser::ensureHandle() {
  if (!handle) handle = xp_css_create();
  if (!handle) LOG_ERR("CSS", "OOM: CSS handle");
  return handle != nullptr;
}

void CssParser::clear() {
  xp_css_destroy(handle);
  handle = nullptr;
}

size_t CssParser::ruleCount() const { return handle ? xp_css_rule_count(handle) : 0; }
bool CssParser::hasFirstLetterRules() const { return handle && xp_css_has_first_letter(handle); }

CssAncestor CssParser::makeAncestor(std::string_view tag, std::string_view classes, std::string_view id) {
  XpCssAncestor wire{};
  xp_css_ancestor(slice(tag), slice(classes), slice(id), &wire);
  CssAncestor result;
  memcpy(&result, &wire, sizeof(result));
  return result;
}

CssStyle CssParser::resolveStyle(std::string_view tag, std::string_view classes, std::string_view id,
                                 const CssAncestor* ancestors, size_t count, bool firstLetter) const {
  CssStyle style;
  if (!handle) return style;
  uint8_t wire[STYLE_WIRE_BYTES];
  xp_css_resolve(handle, slice(tag), slice(classes), slice(id), reinterpret_cast<const XpCssAncestor*>(ancestors),
                 count, firstLetter, wire);
  if (!decodeStyleWire(wire, style)) LOG_ERR("CSS", "Invalid style from CSS core");
  return style;
}

CssStyle CssParser::parseInlineStyle(std::string_view value) {
  uint8_t wire[STYLE_WIRE_BYTES];
  xp_css_inline(reinterpret_cast<const uint8_t*>(value.data()), value.size(), wire);
  CssStyle style;
  if (!decodeStyleWire(wire, style)) LOG_ERR("CSS", "Invalid inline style from CSS core");
  return style;
}

// Cache serialization

// Cache file name (version is CssParser::CSS_CACHE_VERSION)
constexpr char rulesCache[] = "/css_rules.cache";
constexpr char rulesCacheTmp[] = "/css_rules.cache.tmp";
constexpr char rulesCacheBackup[] = "/css_rules.cache.bak";

struct CssParser::IoContext {
  CssParser& owner;
  HalFile owned;
  HalFile& file;
  std::string finalPath, temporaryPath, backupPath;
  std::unique_ptr<uint8_t[]> buffer;
  std::unique_ptr<uint16_t[]> order;
  uint32_t word = 0;
  size_t capacity = 0, count = 0;
  explicit IoContext(CssParser& owner) : owner(owner), file(owned) {}
  IoContext(CssParser& owner, HalFile& source) : owner(owner), file(source) {}
  const std::string& path(const unsigned index, std::string& scratch) const {
    const std::string* paths[] = {&finalPath, &temporaryPath, &backupPath};
    const char* suffixes[] = {rulesCache, rulesCacheTmp, rulesCacheBackup};
    if (!paths[index]->empty()) return *paths[index];
    scratch = owner.cachePath + suffixes[index];
    return scratch;
  }
  static uint32_t operation(void* raw, uint32_t operation, uint32_t argument) {
    auto& c = *static_cast<IoContext*>(raw);
    auto& self = c.owner;
    std::string pathScratch, otherScratch;
    switch (static_cast<XpBookCssOperation>(operation)) {
      case XP_BOOK_CSS_IO_EMPTY_PATH: {
        return self.cachePath.empty();
      }
      case XP_BOOK_CSS_IO_PREPARE_PATHS: {
        if (argument & 1) c.finalPath = self.cachePath + rulesCache;
        if (argument & 2) c.temporaryPath = self.cachePath + rulesCacheTmp;
        if (argument & 4) c.backupPath = self.cachePath + rulesCacheBackup;
        break;
      }
      case XP_BOOK_CSS_IO_EXISTS_FINAL: {
        return Storage.exists(c.path(0, pathScratch).c_str());
      }
      case XP_BOOK_CSS_IO_EXISTS_BACKUP: {
        return Storage.exists(c.path(2, pathScratch).c_str());
      }
      case XP_BOOK_CSS_IO_RENAME_BACKUP_FINAL: {
        return Storage.rename(c.path(2, pathScratch).c_str(), c.path(0, otherScratch).c_str());
      }
      case XP_BOOK_CSS_IO_REMOVE_FINAL: {
        return Storage.remove(c.path(0, pathScratch).c_str());
      }
      case XP_BOOK_CSS_IO_REMOVE_TEMPORARY: {
        return Storage.remove(c.path(1, pathScratch).c_str());
      }
      case XP_BOOK_CSS_IO_REMOVE_BACKUP: {
        return Storage.remove(c.path(2, pathScratch).c_str());
      }
      case XP_BOOK_CSS_IO_RENAME_FINAL_BACKUP: {
        return Storage.rename(c.path(0, pathScratch).c_str(), c.path(2, otherScratch).c_str());
      }
      case XP_BOOK_CSS_IO_RENAME_TEMPORARY_FINAL: {
        return Storage.rename(c.path(1, pathScratch).c_str(), c.path(0, otherScratch).c_str());
      }
      case XP_BOOK_CSS_IO_OPEN_FINAL: {
        return Storage.openFileForRead("CSS", c.path(0, pathScratch), c.file);
      }
      case XP_BOOK_CSS_IO_OPEN_TEMPORARY: {
        return Storage.openFileForWrite("CSS", c.path(1, pathScratch), c.file);
      }
      case XP_BOOK_CSS_IO_CLOSE: {
        return c.file.close();
      }
      case XP_BOOK_CSS_IO_READ_WORD: {
        c.word = 0;
        return c.file.read(&c.word, argument);
      }
      case XP_BOOK_CSS_IO_WORD: {
        return c.word;
      }
      case XP_BOOK_CSS_IO_AVAILABLE: {
        return c.file.available();
      }
      case XP_BOOK_CSS_IO_SKIP: {
        return c.file.seekCur(argument);
      }
      case XP_BOOK_CSS_IO_ENSURE: {
        return self.ensureHandle();
      }
      case XP_BOOK_CSS_IO_CLEAR: {
        self.clear();
        break;
      }
      case XP_BOOK_CSS_IO_ALLOCATE_BUFFER: {
        c.buffer = makeUniqueNoThrow<uint8_t[]>(argument);
        c.capacity = argument;
        return bool(c.buffer);
      }
      case XP_BOOK_CSS_IO_READ_BUFFER: {
        const int count = c.file.read(c.buffer.get(), c.capacity);
        c.count = count > 0 ? count : 0;
        return count;
      }
      case XP_BOOK_CSS_IO_FIRST_BYTE: {
        return c.buffer[0];
      }
      case XP_BOOK_CSS_IO_READ_CACHE_VERSION: {
        return readCssCache(self.handle, reinterpret_cast<const uint8_t*>(&c.word), 1, false);
      }
      case XP_BOOK_CSS_IO_READ_CACHE_BUFFER: {
        return readCssCache(self.handle, c.buffer.get(), c.count, false);
      }
      case XP_BOOK_CSS_IO_READ_CACHE_END: {
        return readCssCache(self.handle, nullptr, 0, true);
      }
      case XP_BOOK_CSS_IO_RULE_COUNT: {
        return self.ruleCount();
      }
      case XP_BOOK_CSS_IO_HAS_HANDLE: {
        return self.handle != nullptr;
      }
      case XP_BOOK_CSS_IO_ALLOCATE_ORDER: {
        c.order = makeUniqueNoThrow<uint16_t[]>(argument);
        return bool(c.order);
      }
      case XP_BOOK_CSS_IO_PREPARE_ORDER: {
        return xp_css_cache_write_begin(self.handle, argument, c.order.get(), self.ruleCount());
      }
      case XP_BOOK_CSS_IO_WRITE_NEXT: {
        c.count = 0;
        return xp_css_cache_write(self.handle, c.order.get(), self.ruleCount(), c.buffer.get(), c.capacity, &c.count);
      }
      case XP_BOOK_CSS_IO_WRITTEN: {
        return c.count;
      }
      case XP_BOOK_CSS_IO_WRITE_BUFFER: {
        return c.file.write(c.buffer.get(), c.count) == c.count;
      }
      case XP_BOOK_CSS_IO_WRITE_WORD: {
        return c.file.write(&argument, sizeof(argument)) == sizeof(argument);
      }
      case XP_BOOK_CSS_IO_DROP_ORDER: {
        c.order.reset();
        break;
      }
      case XP_BOOK_CSS_IO_SOURCE_VALID: {
        return bool(c.file);
      }
      case XP_BOOK_CSS_IO_FEED_BUFFER: {
        return xp_css_feed_with_diagnostics(self.handle, c.buffer.get(), c.count, false, &CSS_DIAGNOSTICS);
      }
      case XP_BOOK_CSS_IO_FEED_END: {
        return xp_css_feed_with_diagnostics(self.handle, nullptr, 0, true, &CSS_DIAGNOSTICS);
      }
      case XP_BOOK_CSS_IO_RESTORE: {
        return self.restoreCacheBackupIfNeeded();
      }
    }
    return 0;
  }
  static void diagnostic(void* raw, uint32_t event, uint32_t argument) {
    auto& c = *static_cast<IoContext*>(raw);
    switch (static_cast<XpBookCssEvent>(event)) {
      case XP_BOOK_CSS_IO_EVENT_RESTORE_FAILED:
        LOG_ERR("CSS", "Failed to restore CSS cache backup");
        break;
      case XP_BOOK_CSS_IO_EVENT_RESTORED:
        LOG_DBG("CSS", "Restored CSS cache backup after interrupted replacement");
        break;
      case XP_BOOK_CSS_IO_EVENT_OUTPUT_OOM:
        LOG_ERR("CSS", "OOM: cache output (%u bytes)", XP_CSS_CACHE_BUFFER_BYTES);
        break;
      case XP_BOOK_CSS_IO_EVENT_ORDER_OOM:
        LOG_ERR("CSS", "OOM sorting CSS cache rules");
        break;
      case XP_BOOK_CSS_IO_EVENT_ORDER_FAILED:
        LOG_ERR("CSS", "Could not prepare cache source order");
        break;
      case XP_BOOK_CSS_IO_EVENT_WRITE_FAILED:
        LOG_ERR("CSS", "Failed to write temporary CSS cache");
        break;
      case XP_BOOK_CSS_IO_EVENT_BACKUP_FAILED:
        LOG_ERR("CSS", "Failed to back up existing CSS cache");
        break;
      case XP_BOOK_CSS_IO_EVENT_PROMOTE_FAILED:
        LOG_ERR("CSS", "Failed to promote temporary CSS cache");
        break;
      case XP_BOOK_CSS_IO_EVENT_ROLLBACK_FAILED:
        LOG_ERR("CSS", "Failed to restore previous CSS cache");
        break;
      case XP_BOOK_CSS_IO_EVENT_SAVED_COMPLETE:
        LOG_DBG("CSS", "Saved %u rules to %s cache", argument, "complete");
        break;
      case XP_BOOK_CSS_IO_EVENT_SAVED_PARTIAL:
        LOG_DBG("CSS", "Saved %u rules to %s cache", argument, "partial");
        break;
      case XP_BOOK_CSS_IO_EVENT_VERSION:
        LOG_DBG("CSS", "Cache version mismatch (got %u, expected %u), removing stale cache for rebuild", argument,
                CSS_CACHE_VERSION);
        break;
      case XP_BOOK_CSS_IO_EVENT_INPUT_OOM:
        LOG_ERR("CSS", "OOM: cache input (%zu bytes)", READ_BUFFER_SIZE);
        break;
      case XP_BOOK_CSS_IO_EVENT_LOADED_COMPLETE:
        LOG_DBG("CSS", "Loaded %u rules from %s cache", argument, "complete");
        break;
      case XP_BOOK_CSS_IO_EVENT_LOADED_PARTIAL:
        LOG_DBG("CSS", "Loaded %u rules from %s cache", argument, "partial");
        break;
      case XP_BOOK_CSS_IO_EVENT_INVALID_SOURCE:
        LOG_ERR("CSS", "Cannot read from invalid file");
        break;
      case XP_BOOK_CSS_IO_EVENT_SOURCE_OOM:
        LOG_ERR("CSS", "OOM: CSS input (%zu bytes)", READ_BUFFER_SIZE);
        break;
      case XP_BOOK_CSS_IO_EVENT_PARSED:
        LOG_DBG("CSS", "Parsed %zu rules from %zu bytes", c.owner.ruleCount(), static_cast<size_t>(argument));
        break;
    }
  }
  XpBookServices services() { return {this, operation, nullptr, diagnostic}; }
};

bool CssParser::hasCache() const { return Storage.exists((cachePath + rulesCache).c_str()); }

uint32_t CssParser::cacheOperation(const uint32_t action, const bool complete) const {
  IoContext context{const_cast<CssParser&>(*this)};
  const auto io = context.services();
  return xp_book_css(&io, action, complete);
}
bool CssParser::restoreCacheBackupIfNeeded() const { return cacheOperation(0); }
void CssParser::deleteCache() const { cacheOperation(1); }
CssParser::CacheStatus CssParser::inspectCache() const { return static_cast<CacheStatus>(cacheOperation(2)); }
bool CssParser::saveToCache(const bool complete) const { return cacheOperation(3, complete); }
CssParser::CacheLoadResult CssParser::loadFromCache() { return static_cast<CacheLoadResult>(cacheOperation(4)); }
CssParser::ParseResult CssParser::loadFromStream(HalFile& source) {
  IoContext context{*this, source};
  const auto io = context.services();
  return static_cast<ParseResult>(xp_book_css(&io, 5, false));
}
