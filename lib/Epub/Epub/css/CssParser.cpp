#include "CssParser.h"

#include <Logging.h>
#include <Memory.h>
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
constexpr size_t MAX_RULES = 1500;
constexpr size_t SELECTOR_POOL_CAP = 32 * 1024;
constexpr size_t MAX_STORED_KEY_LENGTH = 257;
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

CssParser::ParseResult CssParser::loadFromStream(HalFile& source) {
  if (!source) {
    LOG_ERR("CSS", "Cannot read from invalid file");
    return ParseResult::Error;
  }
  if (!ensureHandle()) return ParseResult::Partial;
  auto buffer = makeUniqueNoThrow<uint8_t[]>(READ_BUFFER_SIZE);
  if (!buffer) {
    LOG_ERR("CSS", "OOM: CSS input (%zu bytes)", READ_BUFFER_SIZE);
    return ParseResult::Partial;
  }
  bool partial = false;
  size_t totalRead = 0;
  while (source.available()) {
    const int count = source.read(buffer.get(), READ_BUFFER_SIZE);
    if (count <= 0) break;
    totalRead += static_cast<size_t>(count);
    partial |=
        xp_css_feed_with_diagnostics(handle, buffer.get(), static_cast<size_t>(count), false, &CSS_DIAGNOSTICS) != 0;
  }
  const auto result =
      static_cast<ParseResult>(xp_css_feed_with_diagnostics(handle, nullptr, 0, true, &CSS_DIAGNOSTICS));
  LOG_DBG("CSS", "Parsed %zu rules from %zu bytes", ruleCount(), totalRead);
  if (partial || result == ParseResult::Partial) {
    return ParseResult::Partial;
  }
  return result;
}

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
constexpr uint8_t CSS_CACHE_FLAG_PARTIAL = 1 << 0;
constexpr uint8_t CSS_CACHE_KNOWN_FLAGS = CSS_CACHE_FLAG_PARTIAL;

bool CssParser::hasCache() const { return Storage.exists((cachePath + rulesCache).c_str()); }

bool CssParser::restoreCacheBackupIfNeeded() const {
  if (cachePath.empty()) {
    return false;
  }

  const std::string finalPath = cachePath + rulesCache;
  if (Storage.exists(finalPath.c_str())) {
    return true;
  }

  const std::string backupPath = cachePath + rulesCacheBackup;
  if (!Storage.exists(backupPath.c_str())) {
    return false;
  }

  if (!Storage.rename(backupPath.c_str(), finalPath.c_str())) {
    LOG_ERR("CSS", "Failed to restore CSS cache backup");
    return false;
  }

  LOG_DBG("CSS", "Restored CSS cache backup after interrupted replacement");
  return true;
}

void CssParser::deleteCache() const {
  if (hasCache()) Storage.remove((cachePath + rulesCache).c_str());
  Storage.remove((cachePath + rulesCacheTmp).c_str());
  Storage.remove((cachePath + rulesCacheBackup).c_str());
}

CssParser::CacheStatus CssParser::inspectCache() const {
  if (cachePath.empty() || (!hasCache() && !restoreCacheBackupIfNeeded())) {
    return CacheStatus::Missing;
  }

  HalFile file;
  if (!Storage.openFileForRead("CSS", cachePath + rulesCache, file)) {
    return CacheStatus::Invalid;
  }

  uint8_t version = 0;
  uint8_t flags = 0;
  uint16_t ruleCount = 0;
  if (file.read(&version, sizeof(version)) != sizeof(version) || version != CSS_CACHE_VERSION ||
      file.read(&flags, sizeof(flags)) != sizeof(flags) || (flags & ~CSS_CACHE_KNOWN_FLAGS) != 0 ||
      file.read(&ruleCount, sizeof(ruleCount)) != sizeof(ruleCount) || ruleCount > MAX_RULES) {
    return CacheStatus::Invalid;
  }

  const bool partial = (flags & CSS_CACHE_FLAG_PARTIAL) != 0;
  if (!partial) {
    // Complete caches are fully validated while hydrating, avoiding a second
    // payload scan on every EPUB open.
    return CacheStatus::Complete;
  }

  const auto skipBytes = [&file](const size_t byteCount) {
    return static_cast<size_t>(file.available()) >= byteCount && file.seekCur(byteCount);
  };
  size_t selectorBytes = 0;
  for (uint16_t i = 0; i < ruleCount; ++i) {
    uint16_t selectorLen = 0;
    if (file.read(&selectorLen, sizeof(selectorLen)) != sizeof(selectorLen) || selectorLen == 0 ||
        selectorLen > MAX_STORED_KEY_LENGTH) {
      return CacheStatus::Invalid;
    }
    selectorBytes += selectorLen;
    if (selectorBytes > SELECTOR_POOL_CAP || !skipBytes(static_cast<size_t>(selectorLen) + STYLE_WIRE_BYTES)) {
      return CacheStatus::Invalid;
    }
  }

  if (file.available() != 0) {
    return CacheStatus::Invalid;
  }

  return CacheStatus::Partial;
}

bool CssParser::saveToCache(const bool complete) const {
  if (cachePath.empty()) {
    return false;
  }

  const std::string finalPath = cachePath + rulesCache;
  const std::string tmpPath = cachePath + rulesCacheTmp;
  const std::string backupPath = cachePath + rulesCacheBackup;

  Storage.remove(tmpPath.c_str());

  HalFile file;
  if (!Storage.openFileForWrite("CSS", tmpPath, file)) {
    return false;
  }

  auto buffer = makeUniqueNoThrow<uint8_t[]>(XP_CSS_CACHE_BUFFER_BYTES);
  if (!buffer) {
    LOG_ERR("CSS", "OOM: cache output (%u bytes)", XP_CSS_CACHE_BUFFER_BYTES);
    file.close();
    Storage.remove(tmpPath.c_str());
    return false;
  }
  bool writeOk = true;
  const auto ruleCount = static_cast<uint16_t>(this->ruleCount());
  if (handle) {
    auto order = makeUniqueNoThrow<uint16_t[]>(ruleCount);
    if (!order || xp_css_cache_write_begin(handle, complete, order.get(), ruleCount) != 0) {
      if (!order) {
        LOG_ERR("CSS", "OOM sorting CSS cache rules");
      } else {
        LOG_ERR("CSS", "Could not prepare cache source order");
      }
      file.close();
      Storage.remove(tmpPath.c_str());
      return false;
    }
    while (writeOk) {
      size_t written = 0;
      if (xp_css_cache_write(handle, order.get(), ruleCount, buffer.get(), XP_CSS_CACHE_BUFFER_BYTES, &written) != 0) {
        writeOk = false;
        break;
      }
      if (written == 0) break;
      writeOk = file.write(buffer.get(), written) == written;
    }
  } else {
    const uint8_t header[] = {CSS_CACHE_VERSION, static_cast<uint8_t>(complete ? 0 : CSS_CACHE_FLAG_PARTIAL), 0, 0};
    writeOk = file.write(header, sizeof(header)) == sizeof(header);
  }

  if (!writeOk || !file.close()) {
    LOG_ERR("CSS", "Failed to write temporary CSS cache");
    file.close();
    Storage.remove(tmpPath.c_str());
    return false;
  }

  const bool hadExistingCache = Storage.exists(finalPath.c_str());
  if (hadExistingCache) {
    Storage.remove(backupPath.c_str());
    if (!Storage.rename(finalPath.c_str(), backupPath.c_str())) {
      LOG_ERR("CSS", "Failed to back up existing CSS cache");
      Storage.remove(tmpPath.c_str());
      return false;
    }
  }

  if (!Storage.rename(tmpPath.c_str(), finalPath.c_str())) {
    LOG_ERR("CSS", "Failed to promote temporary CSS cache");
    Storage.remove(tmpPath.c_str());
    if (Storage.exists(backupPath.c_str()) && !Storage.rename(backupPath.c_str(), finalPath.c_str())) {
      LOG_ERR("CSS", "Failed to restore previous CSS cache");
    }
    return false;
  }

  Storage.remove(backupPath.c_str());

  LOG_DBG("CSS", "Saved %u rules to %s cache", ruleCount, complete ? "complete" : "partial");
  return true;
}

CssParser::CacheLoadResult CssParser::loadFromCache() {
  if (cachePath.empty()) return CacheLoadResult::Invalid;
  HalFile file;
  if (!Storage.openFileForRead("CSS", cachePath + rulesCache, file)) {
    if (!restoreCacheBackupIfNeeded() || !Storage.openFileForRead("CSS", cachePath + rulesCache, file)) {
      return CacheLoadResult::Invalid;
    }
  }
  clear();
  uint8_t version = 0;
  if (file.read(&version, 1) != 1 || version != CSS_CACHE_VERSION) {
    LOG_DBG("CSS", "Cache version mismatch (got %u, expected %u), removing stale cache for rebuild", version,
            CSS_CACHE_VERSION);
    file.close();
    Storage.remove((cachePath + rulesCache).c_str());
    return CacheLoadResult::Invalid;
  }
  if (!ensureHandle()) return CacheLoadResult::LowMemory;
  auto buffer = makeUniqueNoThrow<uint8_t[]>(READ_BUFFER_SIZE);
  if (!buffer) {
    LOG_ERR("CSS", "OOM: cache input (%zu bytes)", READ_BUFFER_SIZE);
    clear();
    return CacheLoadResult::LowMemory;
  }
  uint8_t flags = 0;
  bool firstChunk = true;
  int32_t status = readCssCache(handle, &version, 1, false);
  while (status == 0 && file.available()) {
    const int count = file.read(buffer.get(), READ_BUFFER_SIZE);
    if (count <= 0) {
      status = 2;
      break;
    }
    if (firstChunk) {
      flags = buffer[0];
      firstChunk = false;
    }
    status = readCssCache(handle, buffer.get(), static_cast<size_t>(count), false);
  }
  if (status == 0) status = readCssCache(handle, nullptr, 0, true);
  if (status != 0) {
    clear();
    return status == 1 ? CacheLoadResult::LowMemory : CacheLoadResult::Invalid;
  }
  LOG_DBG("CSS", "Loaded %u rules from %s cache", static_cast<unsigned>(ruleCount()),
          (flags & CSS_CACHE_FLAG_PARTIAL) ? "partial" : "complete");
  return CacheLoadResult::Complete;
}
