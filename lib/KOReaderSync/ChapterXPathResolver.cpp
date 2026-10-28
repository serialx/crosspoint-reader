#include "ChapterXPathResolver.h"

#include <Epub/VisibleTextUtils.h>
#include <Logging.h>
#include <Print.h>
#include <Utf8.h>
#include <xp_ffi.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace {
// The XML core preserves Expat error numbers; text and log gating stay in C++.
const char* xmlErrorText(const uint32_t code) {
  static constexpr const char* messages[] = {"unknown XML error",
                                             "out of memory",
                                             "syntax error",
                                             "no element found",
                                             "not well-formed (invalid token)",
                                             "unclosed token",
                                             "partial character",
                                             "mismatched tag",
                                             "duplicate attribute",
                                             "junk after document element",
                                             "illegal parameter entity reference",
                                             "undefined entity",
                                             "recursive entity reference",
                                             "asynchronous entity",
                                             "reference to invalid character number",
                                             "reference to binary entity",
                                             "reference to external entity in attribute",
                                             "XML or text declaration not at start of entity",
                                             "unknown encoding",
                                             "encoding specified in XML declaration is incorrect",
                                             "unclosed CDATA section",
                                             "error in processing external entity reference",
                                             "document is not standalone",
                                             "unexpected parser state - please send a bug report",
                                             "entity declared in parameter entity",
                                             "requested feature requires XML_DTD support in Expat",
                                             "cannot change setting once parsing has begun",
                                             "unbound prefix",
                                             "must not undeclare prefix",
                                             "incomplete markup in parameter entity",
                                             "XML declaration not well-formed",
                                             "text declaration not well-formed",
                                             "illegal character(s) in public id",
                                             "parser suspended",
                                             "parser not suspended",
                                             "parsing aborted"};
  return code < sizeof(messages) / sizeof(messages[0]) ? messages[code] : messages[0];
}
void logXmlError(const uint32_t error, const bool final) {
  if (final) {
    LOG_ERR("KOX", "Final XML parse error: %s", xmlErrorText(error));
  } else {
    LOG_ERR("KOX", "XML parse error: %s", xmlErrorText(error));
  }
}
std::string stripPrefix(const char* name) {
  if (!name) {
    return "";
  }

  const char* local = std::strrchr(name, ':');
  return local ? std::string(local + 1) : std::string(name);
}

struct NameCounter {
  std::string name;
  int count;
};

// Mirrors crengine's IsEmptySpace(): only these four count as empty space.
bool isWhitespaceOnly(const char* data, const int len) {
  for (int i = 0; i < len; i++) {
    const char c = data[i];
    if (c != ' ' && c != '\r' && c != '\n' && c != '\t') {
      return false;
    }
  }
  return true;
}

struct ParentState {
  std::vector<NameCounter> children;

  int nextIndex(const std::string& name) {
    for (auto& child : children) {
      if (child.name == name) {
        child.count++;
        return child.count;
      }
    }

    children.push_back({name, 1});
    return 1;
  }
};

struct PathSegment {
  std::string name;
  int index;
};

std::string buildParagraphXPath(const int spineIndex, const std::vector<PathSegment>& path, const int textNodeIndex,
                                const size_t charOffset) {
  std::string xpath = "/body/DocFragment[" + std::to_string(spineIndex + 1) + "]/body";
  for (const auto& segment : path) {
    xpath += "/" + segment.name + "[" + std::to_string(segment.index) + "]";
  }
  if (textNodeIndex > 0) {
    xpath += "/text()[" + std::to_string(textNodeIndex) + "]." + std::to_string(charOffset);
  }
  return xpath;
}

size_t countUtf8Codepoints(const char* data, const int len) {
  if (!data || len <= 0) {
    return 0;
  }

  size_t count = 0;
  const unsigned char* ptr = reinterpret_cast<const unsigned char*>(data);
  const unsigned char* end = ptr + len;
  while (ptr < end) {
    utf8NextCodepoint(&ptr);
    count++;
  }

  return count;
}

// Owns one synchronous parse; a match stops dispatch before the next event.
class XPathParser : public Print {
 public:
  XPathParser() {
    if (xp_xml_create(&parser) != 0) {
      LOG_ERR("KOX", "Failed to create XML parser");
      parseOk = false;
    }
  }
  XPathParser(const XPathParser&) = delete;
  XPathParser& operator=(const XPathParser&) = delete;
  ~XPathParser() override { xp_xml_destroy(parser); }

  bool ok() const { return parser != nullptr && parseOk; }
  bool finish() {
    if (ok() && !stopped) {
      parseOk = parse(nullptr, 0, true);
      // A match first delivered by finalization was an aborted final parse in Expat.
      if (stopped) {
        logXmlError(35, true);
        parseOk = false;
      }
    }
    return parseOk;
  }
  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t* buffer, size_t size) override {
    if (ok() && !stopped) parseOk = parse(buffer, size, false);
    return size;
  }

 protected:
  virtual void onStartElement(const char* name) = 0;
  virtual void onEndElement(const char* name) = 0;
  virtual void onCharacterData(const char*, int) {}
  virtual void onMarkupBoundary() {}
  bool stopped = false;

 private:
  bool parse(const uint8_t* buffer, size_t size, bool final) {
    size_t offset = 0;
    do {
      size_t consumed = 0;
      if (xp_xml_feed(parser, buffer ? buffer + offset : nullptr, size - offset, final, &consumed) != 0) {
        LOG_ERR("KOX", "Failed to feed XML parser");
        return false;
      }
      offset += consumed;
      for (;;) {
        XpXmlEvent event{};
        const int32_t status = xp_xml_next(parser, &event);
        if (status != 0) {
          if (status == -2) {
            logXmlError(1, final);
          } else {
            LOG_ERR("KOX", "Failed to read XML event");
          }
          return false;
        }
        switch (event.kind) {
          case XP_XML_START:
            onStartElement(event.name.data);
            break;
          case XP_XML_END:
            onEndElement(event.name.data);
            break;
          case XP_XML_TEXT:
            if (event.name.len > INT_MAX) return false;
            onCharacterData(event.name.data, static_cast<int>(event.name.len));
            break;
          case XP_XML_COMMENT:
          case XP_XML_PI:
          case XP_XML_CDATA_START:
          case XP_XML_CDATA_END:
            onMarkupBoundary();
            break;
          case XP_XML_ENTITY:
            break;
          case XP_XML_DONE:
            return offset == size;
          case XP_XML_NEED_INPUT:
            break;
          case XP_XML_ERROR:
            logXmlError(event.error, final);
            return false;
          default:
            LOG_ERR("KOX", "Unexpected XML event %u", event.kind);
            return false;
        }
        if (stopped) return true;
        if (event.kind == XP_XML_NEED_INPUT) break;
      }
      if (consumed == 0 && offset < size) return false;
    } while (offset < size);
    return true;
  }

  XpXml* parser = nullptr;
  bool parseOk = true;
};

class ParagraphTextCounter final : public XPathParser {
 public:
  // Exclude trailing formatting whitespace so 100% resolves inside real text.
  size_t totalVisibleChars() const { return lastTextEndChars; }

 private:
  void onStartElement(const char* rawName) override {
    const std::string name = stripPrefix(rawName);

    if (!insideBody) {
      if (name == "body") {
        insideBody = true;
        bodyDepth = depth;
      }
      depth++;
      return;
    }

    if (nonVisibleDepth > 0 || VisibleTextUtils::isNonVisibleElement(name)) {
      nonVisibleDepth++;
    }
    depth++;
  }

  void onEndElement(const char* rawName) override {
    const std::string name = stripPrefix(rawName);

    depth--;
    if (!insideBody) {
      return;
    }

    if (depth == bodyDepth && name == "body") {
      insideBody = false;
      nonVisibleDepth = 0;
      return;
    }

    if (nonVisibleDepth > 0) {
      nonVisibleDepth--;
    }
  }

  void onCharacterData(const char* data, const int len) override {
    if (!insideBody || nonVisibleDepth > 0 || len <= 0) {
      return;
    }

    visibleChars += countUtf8Codepoints(data, len);
    if (!isWhitespaceOnly(data, len)) {
      // Trailing whitespace inside this run is single-byte, so bytes equal codepoints here.
      int trailing = 0;
      while (trailing < len && isWhitespaceOnly(data + len - 1 - trailing, 1)) {
        trailing++;
      }
      lastTextEndChars = visibleChars - static_cast<size_t>(trailing);
    }
  }

 private:
  bool insideBody = false;
  int depth = 0;
  int bodyDepth = -1;
  uint16_t nonVisibleDepth = 0;
  size_t visibleChars = 0;
  size_t lastTextEndChars = 0;
};

class XPathParagraphResolver final : public XPathParser {
 public:
  explicit XPathParagraphResolver(const int targetParagraph) : targetParagraph(targetParagraph) {}

  bool hasMatch() const { return !xpath.empty(); }
  const std::string& getXPath() const { return xpath; }
  int spineIndex = 0;

 private:
  void onStartElement(const char* rawName) override {
    const std::string name = stripPrefix(rawName);

    if (!insideBody) {
      if (name == "body") {
        insideBody = true;
        bodyDepth = depth;
        parentStates.emplace_back();
      }
      depth++;
      return;
    }

    const int siblingIndex = parentStates.back().nextIndex(name);
    path.push_back({name, siblingIndex});
    parentStates.emplace_back();

    // Count both <p> and <li> as paragraph-like positions, matching how the section
    // layout tracks them (xpathParagraphIndex and xpathListItemIndex). This ensures
    // KOReader progress in list items maps to the correct XPath.
    if (name == "p") {
      paragraphCount++;
    } else if (name == "li") {
      paragraphCount++;
    }
    if (paragraphCount == targetParagraph) {
      xpath = buildParagraphXPath(spineIndex, path, 0, 0);
      stopped = true;
    }

    depth++;
  }

  void onEndElement(const char* rawName) override {
    const std::string name = stripPrefix(rawName);

    depth--;
    if (!insideBody) {
      return;
    }

    if (depth == bodyDepth && name == "body") {
      insideBody = false;
      parentStates.clear();
      path.clear();
      return;
    }

    if (!path.empty()) {
      path.pop_back();
    }
    if (!parentStates.empty()) {
      parentStates.pop_back();
    }
  }

  const int targetParagraph;
  bool insideBody = false;
  int depth = 0;
  int bodyDepth = -1;
  int paragraphCount = 0;
  std::vector<ParentState> parentStates;
  std::vector<PathSegment> path;
  std::string xpath;
};

class XPathProgressResolver final : public XPathParser {
 public:
  enum class BoundaryMode { Exclusive, Inclusive };

  explicit XPathProgressResolver(const size_t targetVisibleChar,
                                 const BoundaryMode boundaryMode = BoundaryMode::Exclusive)
      : targetVisibleChar(targetVisibleChar), boundaryMode(boundaryMode) {}

  bool hasMatch() const { return !xpath.empty(); }
  const std::string& getXPath() const { return xpath; }
  int spineIndex = 0;

 private:
  void onStartElement(const char* rawName) override {
    const std::string name = stripPrefix(rawName);

    if (!insideBody) {
      if (name == "body") {
        insideBody = true;
        bodyDepth = depth;
        parentStates.emplace_back();
        textNodeIndexStack.push_back(0);
      }
      depth++;
      return;
    }

    pendingTextNode = true;
    const int siblingIndex = parentStates.back().nextIndex(name);
    path.push_back({name, siblingIndex});
    parentStates.emplace_back();
    textNodeIndexStack.push_back(0);

    if (nonVisibleDepth > 0 || VisibleTextUtils::isNonVisibleElement(name)) {
      nonVisibleDepth++;
    }

    depth++;
  }

  void onEndElement(const char* rawName) override {
    const std::string name = stripPrefix(rawName);

    depth--;
    if (!insideBody) {
      return;
    }

    if (depth == bodyDepth && name == "body") {
      insideBody = false;
      parentStates.clear();
      path.clear();
      textNodeIndexStack.clear();
      nonVisibleDepth = 0;
      return;
    }

    if (nonVisibleDepth > 0) {
      nonVisibleDepth--;
    }

    pendingTextNode = true;
    if (!textNodeIndexStack.empty()) {
      textNodeIndexStack.pop_back();
    }
    if (!path.empty()) {
      path.pop_back();
    }
    if (!parentStates.empty()) {
      parentStates.pop_back();
    }
  }

  // Visible-offset counting mirrors ChapterHtmlSlimParser::characterData (every body
  // codepoint outside non-visible elements), so offsets recorded by the page LUT line up
  // with what is counted here regardless of which elements hold the text.
  void onCharacterData(const char* data, const int len) override {
    if (!insideBody || nonVisibleDepth > 0 || len <= 0 || stopped) {
      return;
    }

    const size_t codepointCount = countUtf8Codepoints(data, len);
    if (codepointCount == 0) {
      return;
    }

    const bool whitespaceOnly = isWhitespaceOnly(data, len);

    // Start a new text node on first non-empty content after any structural boundary.
    // Only counting non-empty nodes matches KOReader's text()[N] indexing behavior,
    // which skips empty text nodes created by bare <a id="anchor"/> anchors. crengine
    // also drops a whitespace-only run that is the first child of a block element, but
    // "block" there comes from the computed style (display, white-space), which this
    // resolver does not see, so such a run is counted here like any other.
    if (pendingTextNode) {
      if (!textNodeIndexStack.empty()) {
        textNodeIndexStack.back()++;
      }
      textNodeStartChars = visibleChars;
      pendingTextNode = false;
    }

    // Whitespace-only runs are never used as anchors: a target that falls inside one is
    // carried forward and resolves at the start of the next text run instead.
    const size_t nextVisibleChars = visibleChars + codepointCount;
    const bool targetReached = boundaryMode == BoundaryMode::Inclusive ? targetVisibleChar <= nextVisibleChars
                                                                       : targetVisibleChar < nextVisibleChars;
    if (targetReached && !whitespaceOnly) {
      const size_t delta = targetVisibleChar > visibleChars ? targetVisibleChar - visibleChars : 0;
      const int texNode = textNodeIndexStack.empty() ? 0 : textNodeIndexStack.back();
      const size_t charOff = visibleChars - textNodeStartChars + delta;
      xpath = buildParagraphXPath(spineIndex, path, texNode, charOff);
      stopped = true;
      return;
    }

    visibleChars = nextVisibleChars;
  }

  void onMarkupBoundary() override {
    if (!insideBody || nonVisibleDepth > 0 || stopped) {
      return;
    }

    pendingTextNode = true;
  }

  const size_t targetVisibleChar;
  const BoundaryMode boundaryMode;
  bool insideBody = false;
  bool pendingTextNode = true;
  int depth = 0;
  int bodyDepth = -1;
  uint16_t nonVisibleDepth = 0;
  size_t visibleChars = 0;
  size_t textNodeStartChars = 0;
  std::vector<int> textNodeIndexStack;
  std::vector<ParentState> parentStates;
  std::vector<PathSegment> path;
  std::string xpath;
};
}  // namespace

std::string ChapterXPathResolver::findXPathForParagraph(const std::shared_ptr<Epub>& epub, const int spineIndex,
                                                        const uint16_t paragraphIndex) {
  if (!epub || paragraphIndex == 0 || spineIndex < 0 || spineIndex >= epub->getSpineItemsCount()) {
    return "";
  }

  const auto href = epub->getSpineItem(spineIndex).href;
  if (href.empty()) {
    return "";
  }

  XPathParagraphResolver resolver(paragraphIndex);
  if (!resolver.ok()) {
    return "";
  }

  resolver.spineIndex = spineIndex;
  if (!epub->readItemContentsToStream(href, resolver, 1024) || !resolver.finish()) {
    return "";
  }

  if (resolver.hasMatch()) {
    LOG_DBG("KOX", "Resolved paragraph %u in spine %d -> %s", paragraphIndex, spineIndex, resolver.getXPath().c_str());
    return resolver.getXPath();
  }

  LOG_DBG("KOX", "Paragraph %u not found in spine %d", paragraphIndex, spineIndex);
  return "";
}

std::string ChapterXPathResolver::findXPathForVisibleTextOffset(const std::shared_ptr<Epub>& epub, const int spineIndex,
                                                                const uint32_t visibleTextOffset) {
  if (!epub || spineIndex < 0 || spineIndex >= epub->getSpineItemsCount()) {
    return "";
  }

  const auto href = epub->getSpineItem(spineIndex).href;
  if (href.empty()) {
    return "";
  }

  XPathProgressResolver resolver(visibleTextOffset);
  if (!resolver.ok()) {
    return "";
  }

  resolver.spineIndex = spineIndex;
  if (!epub->readItemContentsToStream(href, resolver, 1024) || !resolver.finish()) {
    return "";
  }

  if (resolver.hasMatch()) {
    LOG_DBG("KOX", "Resolved visible offset %u in spine %d -> %s", visibleTextOffset, spineIndex,
            resolver.getXPath().c_str());
    return resolver.getXPath();
  }

  LOG_DBG("KOX", "Visible offset %u not found in spine %d", visibleTextOffset, spineIndex);
  return "";
}

std::string ChapterXPathResolver::findXPathForProgress(const std::shared_ptr<Epub>& epub, const int spineIndex,
                                                       const float intraSpineProgress) {
  if (!epub || spineIndex < 0 || spineIndex >= epub->getSpineItemsCount()) {
    return "";
  }

  const auto href = epub->getSpineItem(spineIndex).href;
  if (href.empty()) {
    return "";
  }

  if (!(intraSpineProgress > 0.0f)) {
    return "/body/DocFragment[" + std::to_string(spineIndex + 1) + "]/body";
  }

  ParagraphTextCounter counter;
  if (!counter.ok() || !epub->readItemContentsToStream(href, counter, 1024) || !counter.finish()) {
    return "";
  }

  const size_t totalVisibleChars = counter.totalVisibleChars();
  if (totalVisibleChars == 0) {
    return "";
  }

  const float clamped = std::max(0.0f, std::min(1.0f, intraSpineProgress));
  const size_t targetVisibleChar =
      std::max<size_t>(1, std::min(totalVisibleChars, static_cast<size_t>(std::ceil(clamped * totalVisibleChars))));

  XPathProgressResolver resolver(targetVisibleChar, XPathProgressResolver::BoundaryMode::Inclusive);
  if (!resolver.ok()) {
    return "";
  }

  resolver.spineIndex = spineIndex;
  if (!epub->readItemContentsToStream(href, resolver, 1024) || !resolver.finish()) {
    return "";
  }

  if (resolver.hasMatch()) {
    LOG_DBG("KOX", "Resolved progress %.3f in spine %d -> %s", intraSpineProgress, spineIndex,
            resolver.getXPath().c_str());
    return resolver.getXPath();
  }

  LOG_DBG("KOX", "Could not resolve progress %.3f in spine %d", intraSpineProgress, spineIndex);
  return "";
}
