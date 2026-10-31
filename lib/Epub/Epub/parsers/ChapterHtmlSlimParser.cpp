#include "ChapterHtmlSlimParser.h"

#include <Logging.h>
#include <Memory.h>
#include <xp_book.h>

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

struct ChapterHtmlSlimParser::PolicyContext {
  ChapterHtmlSlimParser& parser;
  size_t read = 0;
  uint64_t result = 0;
  static uint32_t operation(void* raw, uint32_t operation, uint32_t argument) {
    auto& c = *static_cast<PolicyContext*>(raw);
    auto& self = c.parser;
    switch (static_cast<XpBookParseOperation>(operation)) {
      case XP_BOOK_PARSE_ENSURE: {
        return self.ensureSemantics();
      }
      case XP_BOOK_PARSE_BEGIN_ASSEMBLY: {
        self.beginAssembly();
        break;
      }
      case XP_BOOK_PARSE_OOM: {
        return self.semanticOom();
      }
      case XP_BOOK_PARSE_ALLOCATE_BUFFER: {
        self.parseBuffer_ = makeUniqueNoThrow<uint8_t[]>(argument);
        return bool(self.parseBuffer_);
      }
      case XP_BOOK_PARSE_SET_CAPACITY: {
        self.parseBufferCapacity_ = argument;
        break;
      }
      case XP_BOOK_PARSE_OPEN: {
        return Storage.openFileForRead("EHP", self.filepath, self.parseFile_);
      }
      case XP_BOOK_PARSE_HAS_POPUP: {
        return bool(self.popupFn);
      }
      case XP_BOOK_PARSE_SIZE: {
        return self.parseFile_.size();
      }
      case XP_BOOK_PARSE_POPUP: {
        self.popupFn();
        break;
      }
      case XP_BOOK_PARSE_START_CLOCK: {
        self.parseStartTime_ = millis();
        break;
      }
      case XP_BOOK_PARSE_STOPPED: {
        return self.stopRequested;
      }
      case XP_BOOK_PARSE_READ: {
        c.read = self.parseFile_.read(self.parseBuffer_.get(), argument);
        return c.read;
      }
      case XP_BOOK_PARSE_AVAILABLE: {
        return self.parseFile_.available();
      }
      case XP_BOOK_PARSE_FEED: {
        c.result =
            xp_chapter_feed_diagnostic(self.chapter_, self.parseBuffer_.get(), static_cast<uint32_t>(c.read), argument);
        return static_cast<uint8_t>(c.result);
      }
      case XP_BOOK_PARSE_FEED_ERROR: {
        return static_cast<uint32_t>(c.result) >> 8;
      }
      case XP_BOOK_PARSE_DESTROY: {
        xp_chapter_destroy(self.chapter_);
        self.chapter_ = nullptr;
        break;
      }
      case XP_BOOK_PARSE_RELEASE_BUFFER: {
        self.parseBuffer_.reset();
        break;
      }
      case XP_BOOK_PARSE_IS_OPEN: {
        return self.parseFile_.isOpen();
      }
      case XP_BOOK_PARSE_CLOSE: {
        self.parseFile_.close();
        break;
      }
      case XP_BOOK_PARSE_HAS_CHAPTER: {
        return self.chapter_ != nullptr;
      }
      case XP_BOOK_PARSE_FINISH_ASSEMBLY: {
        return self.finishAssembly();
      }
    }
    return 0;
  }
  static void diagnostic(void* raw, uint32_t event, uint32_t) {
    auto& c = *static_cast<PolicyContext*>(raw);
    switch (static_cast<XpBookParseEvent>(event)) {
      case XP_BOOK_PARSE_EVENT_PARSER_OOM:
        LOG_ERR("EHP", "Couldn't allocate memory for parser");
        break;
      case XP_BOOK_PARSE_EVENT_BUFFER_OOM:
        LOG_ERR("EHP", "Couldn't allocate memory for buffer");
        break;
      case XP_BOOK_PARSE_EVENT_LAYOUT_OOM:
        LOG_ERR("EHP", "Text layout dropped content (OOM); failing section build");
        break;
      case XP_BOOK_PARSE_EVENT_LAYOUT_AFTER_FEED:
        LOG_ERR("EHP", "Text layout dropped content (OOM); failing section build");
        break;
      case XP_BOOK_PARSE_EVENT_LAYOUT_BEFORE_FINISH:
        LOG_ERR("EHP", "Text layout dropped content (OOM); failing section build");
        break;
      case XP_BOOK_PARSE_EVENT_LAYOUT_AFTER_FINISH:
        LOG_ERR("EHP", "Text layout dropped content (OOM); failing section build");
        break;
      case XP_BOOK_PARSE_EVENT_READ_ERROR:
        LOG_ERR("EHP", "File read error");
        break;
      case XP_BOOK_PARSE_EVENT_PARSE_ERROR:
        logParseError(c.result);
        break;
      case XP_BOOK_PARSE_EVENT_TIME:
        LOG_DBG("EHP", "Time to parse and build pages: %lu ms", millis() - c.parser.parseStartTime_);
        break;
    }
  }
};
uint32_t ChapterHtmlSlimParser::parseOperation(const uint32_t action) {
  PolicyContext context{*this};
  const XpBookServices io{&context, PolicyContext::operation, nullptr, PolicyContext::diagnostic};
  return xp_book_parse(&io, action);
}
ChapterHtmlSlimParser::~ChapterHtmlSlimParser() { abortParse(); }
bool ChapterHtmlSlimParser::beginParse() { return parseOperation(0); }
ChapterHtmlSlimParser::ParseStatus ChapterHtmlSlimParser::parseStep() {
  return static_cast<ParseStatus>(parseOperation(1));
}
void ChapterHtmlSlimParser::abortParse() { parseOperation(2); }
bool ChapterHtmlSlimParser::finishParse() { return parseOperation(3); }
bool ChapterHtmlSlimParser::parseAndBuildPages() { return parseOperation(4); }
void ChapterHtmlSlimParser::requestStop() { stopRequested = true; }

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
