#include "ChapterHtmlSlimParser.h"

#include <Logging.h>
#include <Memory.h>

#include "../../../expat/expat.h"

namespace {
[[gnu::cold, gnu::noinline]] void logParseError(const uint64_t result) {
  const auto error = static_cast<uint32_t>(result) >> 8;
  const auto line = static_cast<uint32_t>(result >> 32);
  [[maybe_unused]] const char* message = XML_ErrorString(static_cast<XML_Error>(error));
  if (static_cast<uint8_t>(result) == 1) {
    LOG_DBG("EHP", "Ignoring trailing data after </html>: %s", message);
  } else {
    LOG_ERR("EHP", "Parse error at line %lu:\n%s", static_cast<unsigned long>(line), message);
  }
}
}  // namespace

constexpr size_t MIN_SIZE_FOR_POPUP = 10 * 1024;
constexpr size_t PARSE_BUFFER_SIZE = 1024;

ChapterHtmlSlimParser::~ChapterHtmlSlimParser() { abortParse(); }

bool ChapterHtmlSlimParser::beginParse() {
  if (!ensureSemantics()) {
    LOG_ERR("EHP", "Couldn't allocate memory for parser");
    return false;
  }
  beginAssembly();
  if (semanticOom()) {
    abortParse();
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
  if (semanticOom()) {
    LOG_ERR("EHP", "Text layout dropped content (OOM); failing section build");
    return ParseStatus::Error;
  }

  const size_t len = parseFile_.read(parseBuffer_.get(), PARSE_BUFFER_SIZE);
  if (len == 0 && parseFile_.available() > 0) {
    LOG_ERR("EHP", "File read error");
    return ParseStatus::Error;
  }
  const bool done = parseFile_.available() == 0;
  const auto result = xp_chapter_feed_diagnostic(chapter_, parseBuffer_.get(), static_cast<uint32_t>(len), done);
  const auto status = static_cast<uint8_t>(result);
  if (static_cast<uint32_t>(result) >> 8) {
    logParseError(result);
  } else if (status == 2 && semanticOom()) {
    LOG_ERR("EHP", "Text layout dropped content (OOM); failing section build");
  }
  switch (status) {
    case 0:
      return ParseStatus::More;
    case 1:
      return ParseStatus::Done;
    default:
      return ParseStatus::Error;
  }
}

void ChapterHtmlSlimParser::abortParse() {
  if (chapter_) {
    xp_chapter_destroy(chapter_);
    chapter_ = nullptr;
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
  // The active sink returns this latch after completePageFn unwinds.
}

bool ChapterHtmlSlimParser::finishParse() {
  if (stopRequested) return false;
  // Same check as parseStep(): drops in the final buffer would otherwise slip
  // through because Done is returned before the next step's check runs.
  if (semanticOom()) {
    LOG_ERR("EHP", "Text layout dropped content (OOM); failing section build");
    return false;
  }

  if (chapter_) {
    LOG_DBG("EHP", "Time to parse and build pages: %lu ms", millis() - parseStartTime_);
  }
  parseBuffer_.reset();
  parseBufferCapacity_ = 0;
  parseFile_.close();
  const bool success = finishAssembly();
  if (!stopRequested && semanticOom()) {
    LOG_ERR("EHP", "Text layout dropped content (OOM); failing section build");
  }
  xp_chapter_destroy(chapter_);
  chapter_ = nullptr;
  return success;
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
  return xp_chapter_measure_table(
             semanticState(), &file,
             [](void* ctx, uint8_t* bytes, uint32_t capacity) -> uint32_t {
               return static_cast<HalFile*>(ctx)->read(bytes, capacity);
             },
             &out) != 0;
}

void ChapterHtmlSlimParser::measureTableAtCurrentPosition() {
  if (!chapter_) return;
  HalFile tableFile;
  TableColumnMeasure measure{};
  if (Storage.openFileForRead("EHP", filepath, tableFile) &&
      tableFile.seek(static_cast<size_t>(xp_chapter_byte(semanticState()))) &&
      measureTableColumns(tableFile, measure)) {
    xp_chapter_plan_columns(semanticState(), &measure);
  }
}

uint8_t* ChapterHtmlSlimParser::entityBuffer(const uint32_t length) {
  if (length > parseBufferCapacity_) {
    auto buffer = makeUniqueNoThrow<uint8_t[]>(length);
    if (!buffer) return nullptr;
    parseBuffer_ = std::move(buffer);
    parseBufferCapacity_ = length;
  }
  return parseBuffer_.get();
}
