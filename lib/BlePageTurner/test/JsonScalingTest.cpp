#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <string>

// The test-only ArduinoJson copy increments this in CollectionIterator::next.
size_t bleJsonIteratorSteps = 0;
#include "BlePageTurnerJson.h"

namespace {
struct CountingAllocator : ArduinoJson::Allocator {
  size_t calls = 0;
  void* allocate(size_t size) override {
    ++calls;
    return std::malloc(size);
  }
  void deallocate(void* ptr) override { std::free(ptr); }
  void* reallocate(void* ptr, size_t size) override {
    ++calls;
    return std::realloc(ptr, size);
  }
};

void checkPrefixes(bool remotePrefix, bool bindingPrefix) {
  constexpr uint32_t binding = bleturner::makeBinding(0x030102, false, bleturner::Action::NextPage);
  for (const size_t count : {256u, 512u, 1024u, 2048u, 4096u}) {
    SCOPED_TRACE(count);
    CountingAllocator allocator;
    JsonDocument file(&allocator);
    file["blePageTurnerEnabled"] = 1;
    auto remotes = file["bleRemotes"].to<JsonArray>();
    if (remotePrefix) {
      for (size_t i = 0; i < count; ++i) remotes.add<JsonObject>()["addr"] = "";
    }
    // Two accepted rows also exercise resetting the inner iterator.
    for (const char* addr : {"11:22:33:44:55:01", "11:22:33:44:55:02"}) {
      auto remote = remotes.add<JsonObject>();
      remote["addr"] = addr;
      auto bindings = remote["binds"].to<JsonArray>();
      if (bindingPrefix) {
        for (size_t i = 0; i < count; ++i) bindings.add(0);
      }
      bindings.add(binding);
    }
    ASSERT_FALSE(file.overflowed());
    const auto allocations = allocator.calls;
    bleturner::Config loaded;
    bleJsonIteratorSteps = 0;
    ASSERT_TRUE(bleturner::readJson(loaded, file.as<JsonVariantConst>()));
    const auto steps = bleJsonIteratorSteps;
    EXPECT_EQ(allocator.calls, allocations);
    ASSERT_EQ(loaded.remoteCount, 2);
    EXPECT_STREQ(loaded.remotes[0].addr, "11:22:33:44:55:01");
    EXPECT_STREQ(loaded.remotes[1].addr, "11:22:33:44:55:02");
    for (const auto i : {0, 1}) {
      ASSERT_EQ(loaded.remotes[i].count, 1);
      EXPECT_EQ(loaded.remotes[i].bindings[0], binding);
    }
    bleturner::Config expected;
    expected.enabled = 1;
    expected.remoteCount = 2;
    for (int i = 0; i < 2; ++i) {
      std::snprintf(expected.remotes[i].addr, sizeof(expected.remotes[i].addr), "11:22:33:44:55:0%d", i + 1);
      expected.remotes[i].bindings[0] = binding;
      expected.remotes[i].count = 1;
    }
    JsonDocument actualDoc;
    JsonDocument expectedDoc;
    bleturner::writeJson(loaded, actualDoc);
    bleturner::writeJson(expected, expectedDoc);
    std::string actualText, expectedText;
    serializeJson(actualDoc, actualText);
    serializeJson(expectedDoc, expectedText);
    EXPECT_EQ(actualText, expectedText);
    std::printf("prefix remotes=%d bindings=%d n=%zu advances=%zu read_allocations=%zu\n", remotePrefix, bindingPrefix,
                count, steps, allocator.calls - allocations);
    EXPECT_LE(steps, 16 * count + 128);
  }
}

TEST(BleJsonScalingTest, EmptyAddressPrefixIsLinear) { checkPrefixes(true, false); }
TEST(BleJsonScalingTest, InvalidBindingPrefixIsLinear) { checkPrefixes(false, true); }
TEST(BleJsonScalingTest, NestedInvalidPrefixesAreLinear) { checkPrefixes(true, true); }
}  // namespace
