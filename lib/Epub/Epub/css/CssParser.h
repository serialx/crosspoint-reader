#pragma once

#include <HalStorage.h>

#include <string>
#include <string_view>
#include <utility>

#include "CssStyle.h"

struct XpCss;

/**
 * Lightweight CSS parser for EPUB stylesheets
 *
 * Parses CSS files and extracts styling information relevant for e-ink display.
 * Uses a two-phase approach: first tokenizes the CSS content, then builds
 * a rule database that can be queried during HTML parsing.
 *
 * Supported selectors:
 *   - Compounds of an optional element, #id and up to 4 classes: p, .a, p.a.b, #id, p#id.a
 *   - Descendant and child combinators between compounds: div.poem p, blockquote > p
 *   - Grouped: selector1, selector2 { }
 *   - ::first-letter and legacy :first-letter
 * Matching rules are applied in specificity order (ids, then classes, then elements).
 *
 * Not supported (silently ignored):
 *   - Sibling combinators (+, ~), attribute selectors and other pseudo selectors
 *   - Media queries (content is skipped)
 *   - @import, @font-face, etc.
 */
// Hashed identity of an open element, used to match descendant/child selectors.
struct CssAncestor {
  static constexpr uint8_t MAX_CLASSES = 4;  // classes past the fourth are not matchable
  uint32_t tagHash = 0;
  uint32_t idHash = 0;  // 0 when the element has no id
  uint32_t classHashes[MAX_CLASSES] = {};
  uint8_t classCount = 0;
};

class CssParser {
 public:
  enum class ParseResult : uint8_t {
    Complete,
    Partial,
    Error,
  };

  enum class CacheStatus : uint8_t {
    Missing,
    Complete,
    Partial,
    Invalid,
  };

  enum class CacheLoadResult : uint8_t {
    Complete,
    LowMemory,
    Invalid,
  };

  // Bump when CSS cache format or rules change; section caches are invalidated when this changes
  static constexpr uint8_t CSS_CACHE_VERSION = 19;

  explicit CssParser(std::string cachePath) : cachePath(std::move(cachePath)) {}
  ~CssParser();

  // Non-copyable
  CssParser(const CssParser&) = delete;
  CssParser& operator=(const CssParser&) = delete;

  /**
   * Load and parse CSS from a file stream.
   * Can be called multiple times to accumulate rules from multiple stylesheets.
   * @param source Open file handle to read from
   * @return Complete unless bounded storage stopped rule growth or the source was invalid
   */
  ParseResult loadFromStream(HalFile& source);

  /**
   * Look up the style for an HTML element, merging every matching rule in specificity order.
   *
   * @param tagName The HTML element name (e.g., "p", "div")
   * @param classAttr The class attribute value (may contain multiple space-separated classes)
   * @param idAttr The id attribute value (may be empty)
   * @param ancestors Open ancestors, outermost first; the last entry is the direct parent
   * @param firstLetter Resolve the element's ::first-letter rules instead of its own
   * @return Combined style with all applicable rules merged
   */
  [[nodiscard]] CssStyle resolveStyle(std::string_view tagName, std::string_view classAttr,
                                      std::string_view idAttr = {}, const CssAncestor* ancestors = nullptr,
                                      size_t ancestorCount = 0, bool firstLetter = false) const;

  /** True when any ::first-letter rule is stored, so callers can skip that lookup. */
  [[nodiscard]] bool hasFirstLetterRules() const;

  // Borrowed by the chapter semantic handle; CSS outlives the active parser.
  [[nodiscard]] const XpCss* rustHandle() const { return handle; }

  [[nodiscard]] static CssAncestor makeAncestor(std::string_view tagName, std::string_view classAttr,
                                                std::string_view idAttr);

  /**
   * Parse an inline style attribute string.
   * @param styleValue The value of a style="" attribute
   * @return Parsed style properties
   */
  [[nodiscard]] static CssStyle parseInlineStyle(std::string_view styleValue);

  /**
   * Check if any rules have been loaded
   */
  [[nodiscard]] bool empty() const { return ruleCount() == 0; }

  /**
   * Get count of loaded rule sets
   */
  [[nodiscard]] size_t ruleCount() const;

  /**
   * Clear all loaded rules
   */
  void clear();

  /**
   * Check if CSS rules cache file exists
   */
  bool hasCache() const;

  /** Read the cache header without hydrating its rule map. */
  CacheStatus inspectCache() const;

  /**
   * Delete CSS rules cache file exists
   */
  void deleteCache() const;

  /**
   * Save parsed CSS rules to a cache file.
   * @return true if cache was written successfully
   */
  bool saveToCache(bool complete) const;

  /**
   * Load CSS rules from a cache file.
   * Clears any existing rules before loading.
   * @return Complete when loaded, LowMemory when it should be retried, otherwise Invalid
   */
  CacheLoadResult loadFromCache();

 private:
  XpCss* handle = nullptr;
  std::string cachePath;

  bool ensureHandle();
  bool restoreCacheBackupIfNeeded() const;
};
