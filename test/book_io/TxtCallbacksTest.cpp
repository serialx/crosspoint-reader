#include <gtest/gtest.h>
#include <xp_ffi.h>

#include <cstring>
#include <vector>

namespace {
struct Context {
  std::vector<uint32_t> actions;
  std::vector<uint8_t> output;
  bool read = false;
};

XpBookTxtFileServices services(Context& context) {
  return {&context,
          [](void* opaque, uint32_t action, int32_t, int32_t) {
            static_cast<Context*>(opaque)->actions.push_back(action);
            return true;
          },
          [](void* opaque, uint8_t* buffer, size_t capacity) -> int32_t {
            auto& ctx = *static_cast<Context*>(opaque);
            if (ctx.read || capacity < 3) return 0;
            ctx.read = true;
            memcpy(buffer, "tag", 3);
            return 3;
          },
          [](void* opaque, const uint8_t* buffer, size_t size) {
            auto& output = static_cast<Context*>(opaque)->output;
            output.insert(output.end(), buffer, buffer + size);
            return size;
          },
          [](void*) -> size_t { return 3; },
          [](void*, uint32_t, size_t) {}};
}

bool validate(const XpBookTxtFileServices& io, size_t cached = 3, const char* tag = "tag") {
  return xp_book_txt_validate(&io, cached, reinterpret_cast<const uint8_t*>(tag), strlen(tag));
}
}  // namespace

TEST(BookTxtCallbacks, ConversionAllowsNullSizeAndDiagnostic) {
  for (int slot = 0; slot < 3; ++slot) {
    Context context;
    auto io = services(context);
    if (slot != 1) io.size = nullptr;
    if (slot != 0) io.diagnostic = nullptr;
    ASSERT_TRUE(xp_book_txt_convert(&io, 1, 0));
    EXPECT_EQ(context.output, (std::vector<uint8_t>{'t', 'a', 'g'}));
    EXPECT_EQ(context.actions, (std::vector<uint32_t>{0, 1, 6}));
  }
}

TEST(BookTxtCallbacks, ValidationAllowsNullWrite) {
  Context context;
  auto io = services(context);
  io.write = nullptr;
  EXPECT_TRUE(validate(io));
  EXPECT_EQ(context.actions, (std::vector<uint32_t>{0, 1, 2, 3, 4}));
}

TEST(BookTxtCallbacks, ValidationAllowsSilentInvalidation) {
  for (bool staleSize : {false, true}) {
    Context context;
    auto io = services(context);
    io.diagnostic = nullptr;
    EXPECT_FALSE(validate(io, staleSize ? 4 : 3, staleSize ? "tag" : "other"));
    EXPECT_EQ(context.actions,
              staleSize ? (std::vector<uint32_t>{0, 1, 2, 3, 4, 5}) : (std::vector<uint32_t>{0, 1, 2, 5}));
  }
}

TEST(BookTxtCallbacks, ValidationWithoutCachedSizeAllowsNullSize) {
  Context context;
  auto io = services(context);
  io.size = nullptr;
  EXPECT_TRUE(validate(io, 0));
  EXPECT_EQ(context.actions, (std::vector<uint32_t>{0, 1, 2}));
}

TEST(BookTxtCallbacks, ValidationRejectsMissingRequiredCallbacksBeforeIo) {
  for (int slot = 0; slot < 3; ++slot) {
    Context context;
    auto io = services(context);
    if (slot == 0) io.control = nullptr;
    if (slot == 1) io.read = nullptr;
    if (slot == 2) io.size = nullptr;
    EXPECT_FALSE(validate(io));
    EXPECT_TRUE(context.actions.empty());
  }
}

TEST(BookTxtCallbacks, BmpConversionRejectsMissingRequiredCallbacksBeforeIo) {
  for (int slot = 0; slot < 3; ++slot) {
    Context context;
    auto io = services(context);
    if (slot == 0) io.control = nullptr;
    if (slot == 1) io.read = nullptr;
    if (slot == 2) io.write = nullptr;
    EXPECT_FALSE(xp_book_txt_convert(&io, 1, 0));
    EXPECT_TRUE(context.actions.empty());
    EXPECT_TRUE(context.output.empty());
  }
}

TEST(BookTxtCallbacks, ImageConversionOnlyNeedsControl) {
  for (uint32_t kind : {2u, 3u}) {
    for (int height : {0, 100}) {
      Context context;
      auto io = services(context);
      io.read = nullptr;
      io.write = nullptr;
      io.size = nullptr;
      io.diagnostic = nullptr;
      EXPECT_TRUE(xp_book_txt_convert(&io, kind, height));
      const uint32_t action = height ? (kind == 2 ? 4 : 5) : (kind == 2 ? 2 : 3);
      EXPECT_EQ(context.actions, (std::vector<uint32_t>{0, 1, action, 6}));
    }
  }
}
