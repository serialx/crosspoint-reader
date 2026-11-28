#include <expat.h>
#include <gtest/gtest.h>

#include <cstring>
#include <new>
#include <string>

extern "C" void xp_test_fail_allocation_after(size_t);
extern "C" size_t xp_test_live_allocations();
namespace {
bool failNothrow = false;
}
void* operator new(size_t size, const std::nothrow_t&) noexcept { return failNothrow ? nullptr : ::operator new(size); }
void* operator new[](size_t size, const std::nothrow_t&) noexcept {
  return failNothrow ? nullptr : ::operator new[](size);
}

TEST(ExpatAdapter, HandleAndBufferAllocationFailuresReleaseOwnership) {
  failNothrow = true;
  const auto failed = XML_ParserCreate(nullptr);
  failNothrow = false;
  EXPECT_EQ(failed, nullptr);
  EXPECT_EQ(xp_test_live_allocations(), 0u);
  auto parser = XML_ParserCreate(nullptr);
  ASSERT_NE(parser, nullptr);
  ASSERT_NE(XML_GetBuffer(parser, 16), nullptr);
  failNothrow = true;
  const auto buffer = XML_GetBuffer(parser, 2048);
  failNothrow = false;
  EXPECT_EQ(buffer, nullptr);
  EXPECT_EQ(XML_GetErrorCode(parser), XML_ERROR_NO_MEMORY);
  XML_ParserFree(parser);
  EXPECT_EQ(xp_test_live_allocations(), 0u);
}

TEST(ExpatAdapter, RustAllocationFailuresNeverReturnSuccessfulPartialDocument) {
  const std::string document =
      "<!DOCTYPE root [<!ATTLIST child id CDATA 'default'>]><root><child>" + std::string(3000, 'x') + "</child></root>";
  bool completed = false;
  for (size_t allowance = 0; allowance < 128; ++allowance) {
    xp_test_fail_allocation_after(allowance);
    auto parser = XML_ParserCreate(nullptr);
    XML_Status status = XML_STATUS_ERROR;
    XML_Error error = XML_ERROR_NO_MEMORY;
    if (parser) {
      status = XML_Parse(parser, document.data(), static_cast<int>(document.size()), XML_TRUE);
      error = XML_GetErrorCode(parser);
      XML_ParserFree(parser);
    }
    xp_test_fail_allocation_after(SIZE_MAX);
    EXPECT_EQ(xp_test_live_allocations(), 0u) << allowance;
    if (status == XML_STATUS_OK) {
      completed = true;
      break;
    }
    EXPECT_EQ(error, XML_ERROR_NO_MEMORY) << allowance;
  }
  EXPECT_TRUE(completed);
}

TEST(ExpatAdapter, CallbackStopLatchesUntilFree) {
  auto parser = XML_ParserCreate(nullptr);
  ASSERT_NE(parser, nullptr);
  struct State {
    XML_Parser parser;
    int starts = 0;
    int ends = 0;
  } state{parser};
  XML_SetUserData(parser, &state);
  EXPECT_EQ(XML_GetUserData(parser), &state);
  XML_SetElementHandler(
      parser,
      [](void* ctx, const XML_Char*, const XML_Char**) {
        auto& s = *static_cast<State*>(ctx);
        ++s.starts;
        EXPECT_EQ(XML_StopParser(s.parser, XML_FALSE), XML_STATUS_OK);
      },
      [](void* ctx, const XML_Char*) { ++static_cast<State*>(ctx)->ends; });
  const char* input = "<root><child/></root>";
  EXPECT_EQ(XML_Parse(parser, input, static_cast<int>(strlen(input)), XML_TRUE), XML_STATUS_ERROR);
  EXPECT_EQ(XML_GetErrorCode(parser), XML_ERROR_ABORTED);
  EXPECT_EQ(state.starts, 1);
  EXPECT_EQ(state.ends, 0);
  EXPECT_EQ(XML_Parse(parser, nullptr, 0, XML_TRUE), XML_STATUS_ERROR);
  XML_ParserFree(parser);
  EXPECT_EQ(xp_test_live_allocations(), 0u);
}

TEST(ExpatAdapter, BufferFeedsRetainTextAcrossLongWrites) {
  const std::string expected(6000, 'a');
  const std::string document = "<root>" + expected + "</root>";
  auto parser = XML_ParserCreate(nullptr);
  ASSERT_NE(parser, nullptr);
  std::string result;
  XML_SetUserData(parser, &result);
  XML_SetCharacterDataHandler(
      parser, [](void* ctx, const XML_Char* text, int len) { static_cast<std::string*>(ctx)->append(text, len); });
  void* buffer = XML_GetBuffer(parser, static_cast<int>(document.size()));
  ASSERT_NE(buffer, nullptr);
  memcpy(buffer, document.data(), document.size());
  EXPECT_EQ(XML_ParseBuffer(parser, static_cast<int>(document.size()), XML_TRUE), XML_STATUS_OK);
  EXPECT_EQ(result, expected);
  EXPECT_EQ(XML_Parse(parser, nullptr, 0, XML_TRUE), XML_STATUS_ERROR);
  EXPECT_EQ(XML_GetErrorCode(parser), XML_ERROR_FINISHED);
  XML_ParserFree(parser);
  EXPECT_EQ(xp_test_live_allocations(), 0u);
}

TEST(ExpatAdapter, InvalidBufferLengthDoesNotReadOutOfBounds) {
  auto parser = XML_ParserCreate(nullptr);
  ASSERT_NE(parser, nullptr);
  ASSERT_NE(XML_GetBuffer(parser, 4), nullptr);
  EXPECT_EQ(XML_ParseBuffer(parser, 5, XML_TRUE), XML_STATUS_ERROR);
  EXPECT_EQ(XML_GetErrorCode(parser), XML_ERROR_INVALID_ARGUMENT);
  XML_ParserFree(parser);
  EXPECT_EQ(xp_test_live_allocations(), 0u);
}
