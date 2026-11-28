#include <gtest/gtest.h>
#include <xp_book.h>
#include <xp_ffi.h>

#include <array>
#include <cstring>
#include <set>
#include <vector>

namespace {
struct Trace {
  std::vector<size_t> actions;
  std::set<uint32_t> events;
  uint32_t fail = UINT32_MAX;
  void action(size_t op, size_t a = 0, size_t b = 0) { actions.insert(actions.end(), {op, a, b}); }
};
XpBookBorrow borrow(const char* value) { return {reinterpret_cast<const uint8_t*>(value), strlen(value)}; }
void log(void* ctx, uint32_t event) { static_cast<Trace*>(ctx)->events.insert(event); }
void logValues(void* ctx, uint32_t event, size_t, size_t) { log(ctx, event); }
void logBytes(void* ctx, uint32_t event, size_t) { log(ctx, event); }

struct Document : Trace {
  uint32_t empty = UINT32_MAX;
  bool cover = false;
};
XpBookDocumentServices document(Document& ctx, bool silent) {
  return {&ctx,
          [](void* ptr, uint32_t op, size_t arg) -> size_t {
            auto& c = *static_cast<Document*>(ptr);
            c.action(op, arg);
            return op != c.fail;
          },
          [](void* ptr, uint32_t field) -> XpBookBorrow {
            auto& c = *static_cast<Document*>(ptr);
            c.action(1000, field);
            if (field == c.empty || (field == XP_PKG_COVER && !c.cover)) return {nullptr, 0};
            if (field == XP_BOOK_DOCUMENT_AUXILIARY) return borrow("<img src=\"cover.png\">");
            return borrow("item");
          },
          [](void* ptr, uint32_t field, XpBookBorrow value) {
            static_cast<Document*>(ptr)->action(1001, field, value.len);
          },
          silent ? nullptr : log};
}
TEST(BookResidualCallbacks, DocumentOptionalSinkPreservesAllPasses) {
  std::set<uint32_t> events;
  for (uint32_t kind : {XP_PKG_CONTAINER, XP_PKG_NCX_PASS, XP_PKG_NAV_PASS, XP_PKG_OPF_PASS}) {
    for (uint32_t fail :
         {UINT32_MAX, uint32_t(XP_BOOK_DOCUMENT_FIND), uint32_t(XP_BOOK_DOCUMENT_SIZE),
          uint32_t(XP_BOOK_DOCUMENT_SETUP), uint32_t(XP_BOOK_DOCUMENT_READ), uint32_t(XP_BOOK_DOCUMENT_GUIDE_LOAD)}) {
      for (uint32_t empty :
           {UINT32_MAX, uint32_t(XP_BOOK_DOCUMENT_CONTAINER_PATH), uint32_t(XP_BOOK_DOCUMENT_AUXILIARY)}) {
        for (bool metadata : {false, true}) {
          Document noisy, quiet;
          noisy.fail = quiet.fail = fail;
          noisy.empty = quiet.empty = empty;
          auto a = document(noisy, false), b = document(quiet, true);
          EXPECT_EQ(xp_book_document(&a, kind, metadata), xp_book_document(&b, kind, metadata));
          EXPECT_EQ(noisy.actions, quiet.actions);
          EXPECT_TRUE(quiet.events.empty());
          events.insert(noisy.events.begin(), noisy.events.end());
        }
      }
    }
  }
  EXPECT_EQ(events.size(), 24u);
}
struct Css : Trace {
  size_t heap = 65536, size = 128, parsed = XP_BOOK_CSS_PARSE_COMPLETE;
  bool empty = false, dedup = true;
  std::array<XpBookCssEntry, 2> entries{};
};
XpBookCssParseServices css(Css& ctx, bool silent) {
  return {&ctx,
          [](void* ptr, uint32_t op, size_t index, size_t arg) -> size_t {
            auto& c = *static_cast<Css*>(ptr);
            c.action(op, index, arg);
            if (op == c.fail) return 0;
            switch (op) {
              case XP_BOOK_CSS_FILE_DEDUP:
                return c.dedup ? reinterpret_cast<size_t>(c.entries.data()) : 0;
              case XP_BOOK_CSS_FILE_HEAP:
                return c.heap;
              case XP_BOOK_CSS_FILE_SIZE:
                return c.size;
              case XP_BOOK_CSS_FILE_PARSE:
                return c.parsed;
              case XP_BOOK_CSS_FILE_EMPTY:
                return c.empty;
              default:
                return 1;
            }
          },
          silent ? nullptr : logValues};
}
TEST(BookResidualCallbacks, CssOptionalSinkPreservesResultsAndCleanup) {
  std::set<uint32_t> events;
  for (size_t count : {0, 1, 2}) {
    for (int scenario = 0; scenario < 12; ++scenario) {
      Css noisy;
      switch (scenario) {
        case 1:
          noisy.dedup = false;
          break;
        case 2:
          noisy.heap = 65535;
          break;
        case 3:
          noisy.size = 131073;
          break;
        case 4:
          noisy.fail = XP_BOOK_CSS_FILE_OPEN_WRITE;
          break;
        case 5:
          noisy.fail = XP_BOOK_CSS_FILE_STREAM;
          break;
        case 6:
          noisy.fail = XP_BOOK_CSS_FILE_OPEN_READ;
          break;
        case 7:
          noisy.parsed = XP_BOOK_CSS_PARSE_ERROR;
          break;
        case 8:
          noisy.parsed = XP_BOOK_CSS_PARSE_PARTIAL;
          noisy.empty = true;
          break;
        case 9:
          noisy.parsed = XP_BOOK_CSS_PARSE_PARTIAL;
          break;
        case 10:
          noisy.fail = XP_BOOK_CSS_FILE_SAVE;
          break;
        case 11:
          noisy.entries[0].contentKey = noisy.entries[1].contentKey = 123;
          break;
      }
      Css quiet = noisy;
      auto a = css(noisy, false), b = css(quiet, true);
      EXPECT_EQ(xp_book_css_parse_files(&a, count, 1), xp_book_css_parse_files(&b, count, 1));
      EXPECT_EQ(noisy.actions, quiet.actions);
      EXPECT_TRUE(quiet.events.empty());
      events.insert(noisy.events.begin(), noisy.events.end());
    }
  }
  EXPECT_EQ(events.size(), 14u);
}
struct Zip : Trace {
  size_t size = 22;
  std::array<uint8_t, 22> bytes{};
  bool allocate = true, read = true, signature = true;
};
XpBookZipDetailServices zip(Zip& ctx, bool silent) {
  return {&ctx,
          [](void* ptr) {
            static_cast<Zip*>(ptr)->action(0);
            return true;
          },
          [](void* ptr) {
            auto& c = *static_cast<Zip*>(ptr);
            c.action(1);
            return c.size;
          },
          [](void* ptr, size_t bytes) -> uint8_t* {
            auto& c = *static_cast<Zip*>(ptr);
            c.action(2, bytes);
            return c.allocate ? c.bytes.data() : nullptr;
          },
          [](void* ptr, uint32_t offset, uint8_t* bytes, size_t count) {
            auto& c = *static_cast<Zip*>(ptr);
            c.action(3, offset, count);
            if (c.signature) {
              bytes[0] = 'P';
              bytes[1] = 'K';
              bytes[2] = 5;
              bytes[3] = 6;
            }
            return c.read;
          },
          [](void* ptr, const XpZipDetails* details) { static_cast<Zip*>(ptr)->action(4, details->entries); },
          silent ? nullptr : log};
}
TEST(BookResidualCallbacks, ZipOptionalSinkPreservesFailuresAndSave) {
  std::set<uint32_t> events;
  for (int scenario = 0; scenario < 6; ++scenario) {
    Zip noisy;
    switch (scenario) {
      case 1:
        noisy.size = 21;
        break;
      case 2:
        noisy.size = size_t(UINT32_MAX) + 1;
        break;
      case 3:
        noisy.allocate = false;
        break;
      case 4:
        noisy.read = false;
        break;
      case 5:
        noisy.signature = false;
        break;
    }
    Zip quiet = noisy;
    auto a = zip(noisy, false), b = zip(quiet, true);
    EXPECT_EQ(xp_book_zip_details(&a, false), xp_book_zip_details(&b, false));
    EXPECT_EQ(noisy.actions, quiet.actions);
    EXPECT_TRUE(quiet.events.empty());
    events.insert(noisy.events.begin(), noisy.events.end());
  }
  EXPECT_EQ(events.size(), 4u);
}
struct Xtc : Trace {
  bool prepare = true, copied = false;
};
XpBookXtcCoverServices xtc(Xtc& ctx, bool silent) {
  return {&ctx,
          [](void* ptr, uint32_t op) -> size_t {
            auto& c = *static_cast<Xtc*>(ptr);
            c.action(op);
            if (op == XP_BOOK_XTC_EXISTS) return c.copied;
            if (op == c.fail) return 0;
            if (op == XP_BOOK_XTC_COPY) c.copied = true;
            return 1;
          },
          [](void* ptr) -> XpBookXtcInfo {
            static_cast<Xtc*>(ptr)->action(1000);
            return {16, 16, 1};
          },
          [](void* ptr, size_t bitmap, size_t row, const XpBookXtcThumbnail* plan) {
            auto& c = *static_cast<Xtc*>(ptr);
            c.action(1001, bitmap, row);
            c.action(1002, plan ? plan->height : 0);
            return c.prepare;
          },
          silent ? nullptr : logBytes};
}
TEST(BookResidualCallbacks, XtcOptionalSinkPreservesCoverAndThumbnailTransactions) {
  std::set<uint32_t> events;
  for (bool thumb : {false, true}) {
    for (int height : {8, 32}) {
      for (uint32_t fail = 0; fail <= 13; ++fail) {
        Xtc noisy, quiet;
        noisy.fail = quiet.fail = fail;
        noisy.prepare = quiet.prepare = fail != 13;
        auto a = xtc(noisy, false), b = xtc(quiet, true);
        EXPECT_EQ(xp_book_xtc_generate(&a, thumb, height), xp_book_xtc_generate(&b, thumb, height));
        EXPECT_EQ(noisy.actions, quiet.actions);
        EXPECT_EQ(noisy.copied, quiet.copied);
        EXPECT_TRUE(quiet.events.empty());
        events.insert(noisy.events.begin(), noisy.events.end());
      }
    }
  }
  EXPECT_EQ(events.size(), 13u);
}
size_t clears = 0, reported = 0;
void clear() { ++clears; }
void reclaimed(size_t bytes) { reported = bytes; }
TEST(BookResidualCallbacks, ReclaimOptionalSinkPreservesThreshold) {
  for (size_t before : {65535, 65536}) {
    clears = reported = 0;
    xp_book_reclaim_content(before, clear, reclaimed);
    const auto expected = clears;
    EXPECT_EQ(reported, before < 65536 ? before : 0);
    clears = reported = 0;
    xp_book_reclaim_content(before, clear, nullptr);
    EXPECT_EQ(clears, expected);
    EXPECT_EQ(reported, 0u);
  }
}
}  // namespace
