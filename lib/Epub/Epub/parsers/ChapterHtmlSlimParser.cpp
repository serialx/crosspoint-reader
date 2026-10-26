#include "ChapterHtmlSlimParser.h"

#include <Logging.h>
#include <Memory.h>

constexpr size_t MIN_SIZE_FOR_POPUP = 10 * 1024;
constexpr size_t PARSE_BUFFER_SIZE = 1024;

ChapterHtmlSlimParser::~ChapterHtmlSlimParser() { abortParse(); }

bool ChapterHtmlSlimParser::beginParse() {
  beginAssembly();

  if (xp_xml_create(&xmlParser_) != 0) {
    LOG_ERR("EHP", "Couldn't allocate memory for parser");
    return false;
  }
  // Reuse one read buffer across parse steps; keep it off the render task stack.
  parseBuffer_ = makeUniqueNoThrow<uint8_t[]>(PARSE_BUFFER_SIZE);
  if (!parseBuffer_) {
    LOG_ERR("EHP", "Couldn't allocate memory for buffer");
    abortParse();
    return false;
  }
  parseBufferCapacity_ = PARSE_BUFFER_SIZE;
  xmlByteIndex_ = 0;

  if (!Storage.openFileForRead("EHP", filepath, parseFile_)) {
    abortParse();
    return false;
  }

  // Get file size to decide whether to show indexing popup.
  if (popupFn && parseFile_.size() >= MIN_SIZE_FOR_POPUP) {
    popupFn();
  }

  parseStartTime_ = millis();
  return true;
}

ChapterHtmlSlimParser::ParseStatus ChapterHtmlSlimParser::parseStep() {
  if (stopRequested) return ParseStatus::Error;
  // Layout OOM latched during the previous buffer's callbacks: fail the build
  // instead of emitting pages with silently missing text.
  if (layoutOom || (currentTextBlock && currentTextBlock->hadDroppedWords())) {
    LOG_ERR("EHP", "Text layout dropped content (OOM); failing section build");
    return ParseStatus::Error;
  }

  const size_t len = parseFile_.read(parseBuffer_.get(), PARSE_BUFFER_SIZE);
  if (len == 0 && parseFile_.available() > 0) {
    LOG_ERR("EHP", "File read error");
    return ParseStatus::Error;
  }
  const bool done = parseFile_.available() == 0;
  size_t consumed = 0;
  if (xp_xml_feed(xmlParser_, parseBuffer_.get(), len, done, &consumed) != 0 || consumed != len) {
    LOG_ERR("EHP", "Could not feed XML parser");
    return ParseStatus::Error;
  }
  for (;;) {
    XpXmlEvent event{};
    if (xp_xml_next(xmlParser_, &event) != 0) {
      LOG_ERR("EHP", "Could not read XML event");
      return ParseStatus::Error;
    }
    xmlByteIndex_ = event.byte;
    switch (event.kind) {
      case XP_XML_START:
        startElement(static_cast<ChapterSemantics*>(this), event.name.data, event.attributes);
        break;
      case XP_XML_END:
        endElement(static_cast<ChapterSemantics*>(this), event.name.data);
        break;
      case XP_XML_TEXT:
        if (event.name.len > INT_MAX) return ParseStatus::Error;
        characterData(static_cast<ChapterSemantics*>(this), event.name.data, static_cast<int>(event.name.len));
        break;
      case XP_XML_ENTITY: {
        if (event.name.len > INT_MAX - 2) return ParseStatus::Error;
        const size_t length = event.name.len + 2;
        // Feed copied the input, so reuse its buffer for Expat's &name; spelling.
        if (length > parseBufferCapacity_) {
          auto buffer = makeUniqueNoThrow<uint8_t[]>(length);
          if (!buffer) return ParseStatus::Error;
          parseBuffer_ = std::move(buffer);
          parseBufferCapacity_ = length;
        }
        parseBuffer_[0] = '&';
        memcpy(parseBuffer_.get() + 1, event.name.data, event.name.len);
        parseBuffer_[length - 1] = ';';
        defaultHandlerExpand(static_cast<ChapterSemantics*>(this), reinterpret_cast<const char*>(parseBuffer_.get()),
                             static_cast<int>(length));
        break;
      }
      case XP_XML_COMMENT:
      case XP_XML_PI:
      case XP_XML_CDATA_START:
      case XP_XML_CDATA_END:
        break;
      case XP_XML_DONE:
        return ParseStatus::Done;
      case XP_XML_NEED_INPUT:
        return done ? ParseStatus::Done : ParseStatus::More;
      case XP_XML_ERROR:
        if (htmlEnded_) {
          LOG_DBG("EHP", "Ignoring trailing data after </html>: XML error %u", event.error);
          return ParseStatus::Done;
        }
        LOG_ERR("EHP", "XML error %u at line %llu", event.error, static_cast<unsigned long long>(event.line));
        return ParseStatus::Error;
      case XP_XML_STOPPED:
      default:
        return ParseStatus::Error;
    }
    // A page callback may request cancellation; release its borrowed spans first.
    if (stopRequested) {
      xp_xml_stop(xmlParser_);
      return ParseStatus::Error;
    }
  }
}

void ChapterHtmlSlimParser::abortParse() {
  if (xmlParser_) {
    xp_xml_destroy(xmlParser_);
    xmlParser_ = nullptr;
  }
  parseBuffer_.reset();
  parseBufferCapacity_ = 0;
  // Only close the file if it was successfully opened in beginParse()
  if (parseFile_.isOpen()) {
    parseFile_.close();
  }
}

void ChapterHtmlSlimParser::requestStop() {
  if (stopRequested) return;
  stopRequested = true;
}

bool ChapterHtmlSlimParser::finishParse() {
  if (stopRequested) return false;
  // Same check as parseStep(): drops in the final buffer would otherwise slip
  // through because Done is returned before the next step's check runs.
  if (layoutOom || (currentTextBlock && currentTextBlock->hadDroppedWords())) {
    LOG_ERR("EHP", "Text layout dropped content (OOM); failing section build");
    return false;
  }

  if (xmlParser_) {
    LOG_DBG("EHP", "Time to parse and build pages: %lu ms", millis() - parseStartTime_);
    xp_xml_destroy(xmlParser_);
    xmlParser_ = nullptr;
  }
  parseBuffer_.reset();
  parseBufferCapacity_ = 0;
  parseFile_.close();

  return finishAssembly();
}

bool ChapterHtmlSlimParser::parseAndBuildPages() {
  if (!beginParse()) {
    return false;
  }
  for (;;) {
    const ParseStatus status = parseStep();
    if (status == ParseStatus::Error) {
      abortParse();
      return false;
    }
    if (status == ParseStatus::Done) {
      break;
    }
  }
  return finishParse();
}

bool ChapterHtmlSlimParser::measureTableColumns(HalFile& file, TableColumnMeasure& out) const {
  class FileSource final : public ChapterTableSource {
    HalFile& file;

   public:
    explicit FileSource(HalFile& file) : file(file) {}
    size_t read(char* bytes, size_t capacity) override { return file.read(bytes, capacity); }
  } source(file);
  return ChapterSemantics::measureTableColumns(source, out);
}

void ChapterHtmlSlimParser::measureTableAtCurrentPosition() {
  if (!xmlParser_) return;
  HalFile tableFile;
  TableColumnMeasure measure;
  if (Storage.openFileForRead("EHP", filepath, tableFile) && tableFile.seek(static_cast<size_t>(xmlByteIndex_)) &&
      measureTableColumns(tableFile, measure)) {
    planTableColumns(measure);
  }
}
