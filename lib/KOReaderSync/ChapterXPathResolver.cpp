#include "ChapterXPathResolver.h"

#include <Logging.h>
#include <Print.h>
#include <expat.h>
#include <xp_ffi.h>

#include <string>
namespace {
void logXmlError(uint32_t code, bool final) {
  const char* message = XML_ErrorString(static_cast<XML_Error>(code));
  if (!message) message = "unknown XML error";
  if (final)
    LOG_ERR("KOX", "Final XML parse error: %s", message);
  else
    LOG_ERR("KOX", "XML parse error: %s", message);
}
class XPathParser : public Print {
  XpSyncXPath* parser = nullptr;
  bool valid = true;

 protected:
  XPathParser(uint32_t mode, int spine, size_t target) {
    if (xp_sync_xpath_create(mode, spine, target, &parser) != 0) {
      LOG_ERR("KOX", "Failed to create XML parser");
      valid = false;
    }
  }

 public:
  XPathParser(const XPathParser&) = delete;
  XPathParser& operator=(const XPathParser&) = delete;
  ~XPathParser() override { xp_sync_xpath_destroy(parser); }
  bool ok() const { return valid; }
  bool finish() {
    if (valid) {
      const auto error = xp_sync_xpath_feed(parser, {nullptr, 0}, 1);
      if (error) {
        logXmlError(error, true);
        valid = false;
      }
    }
    return valid;
  }
  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t* data, size_t len) override {
    if (valid) {
      const auto error = xp_sync_xpath_feed(parser, {data, len}, 0);
      if (error) {
        logXmlError(error, false);
        valid = false;
      }
    }
    return len;
  }
  bool hasMatch() const { return xp_sync_xpath_output(parser).len != 0; }
  std::string getXPath() const {
    const auto s = xp_sync_xpath_output(parser);
    return {reinterpret_cast<const char*>(s.data), s.len};
  }
  size_t totalVisibleChars() const { return xp_sync_xpath_count(parser); }
};
class XPathParagraphResolver final : public XPathParser {
 public:
  XPathParagraphResolver(int spine, uint16_t target) : XPathParser(0, spine, target) {}
};
class XPathProgressResolver final : public XPathParser {
 public:
  XPathProgressResolver(int spine, size_t target, bool inclusive = false)
      : XPathParser(inclusive ? 2 : 1, spine, target) {}
};
class ParagraphTextCounter final : public XPathParser {
 public:
  ParagraphTextCounter() : XPathParser(3, 0, 0) {}
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

  XPathParagraphResolver resolver(spineIndex, paragraphIndex);
  if (!resolver.ok()) {
    return "";
  }

  if (!epub->readItemContentsToStream(href, resolver, 1024) || !resolver.finish()) {
    return "";
  }

  if (resolver.hasMatch()) {
    const std::string result = resolver.getXPath();
    LOG_DBG("KOX", "Resolved paragraph %u in spine %d -> %s", paragraphIndex, spineIndex, result.c_str());
    return result;
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

  XPathProgressResolver resolver(spineIndex, visibleTextOffset);
  if (!resolver.ok()) {
    return "";
  }

  if (!epub->readItemContentsToStream(href, resolver, 1024) || !resolver.finish()) {
    return "";
  }

  if (resolver.hasMatch()) {
    const std::string result = resolver.getXPath();
    LOG_DBG("KOX", "Resolved visible offset %u in spine %d -> %s", visibleTextOffset, spineIndex, result.c_str());
    return result;
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

  const size_t targetVisibleChar = xp_sync_xpath_target(totalVisibleChars, intraSpineProgress);
  XPathProgressResolver resolver(spineIndex, targetVisibleChar, true);
  if (!resolver.ok()) {
    return "";
  }

  if (!epub->readItemContentsToStream(href, resolver, 1024) || !resolver.finish()) {
    return "";
  }

  if (resolver.hasMatch()) {
    const std::string result = resolver.getXPath();
    LOG_DBG("KOX", "Resolved progress %.3f in spine %d -> %s", intraSpineProgress, spineIndex, result.c_str());
    return result;
  }

  LOG_DBG("KOX", "Could not resolve progress %.3f in spine %d", intraSpineProgress, spineIndex);
  return "";
}
