// The host HalFile fakes must reject what production asserts on and keep the cleanup
// production allows. lib/hal/HalStorage.cpp asserts impl != nullptr in close/read/write/
// seek/size/isDirectory/... , installs an impl for every open attempt and every
// openNextFile result, keeps it across close, and leaves a moved-from source without one.
// This suite pins both directions with the library-builder fake.
#include <HalStorage.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <utility>

#include "../hal_contract/Contract.h"

namespace {

constexpr const char* kNoImpl = "no impl";

void readByte(HalFile& file) {
  uint8_t byte = 0;
  file.read(&byte, sizeof(byte));
}

void readName(HalFile& file) {
  char name[8] = {};
  file.getName(name, sizeof(name));
}

}  // namespace

class HalFileContract : public ::testing::Test {
 protected:
  void SetUp() override { fake::reset(); }
};

TEST_F(HalFileContract, DefaultHandleRejectsEveryAssertedOperation) {
  HalFile file;
  hal_contract::rejectsOperations(file);
}

TEST_F(HalFileContract, SafeQueriesStaySafeWithoutAnImpl) {
  HalFile file;
  EXPECT_FALSE(file.isOpen());
  EXPECT_FALSE(static_cast<bool>(file));
  EXPECT_EQ(file.modificationTime(), 0u);
}

TEST_F(HalFileContract, FailedOpenStillLeavesACloseableHandle) {
  HalFile file;
  EXPECT_FALSE(Storage.openFileForRead("TEST", "/missing", file));
  file.close();  // HalStorage installs an impl even when the open fails.
  file.close();  // Repeated close stays valid: close keeps the impl.
}

TEST_F(HalFileContract, RepeatedCloseAndQueriesAfterCloseStayValid) {
  fake::add("/book", "bytes");
  HalFile file = Storage.open("/book");
  ASSERT_TRUE(file);
  EXPECT_TRUE(file.close());
  file.close();  // Close keeps the impl, so a repeated close is not an invalid operation.
  EXPECT_FALSE(file.isOpen());
  // The handle still answers after close; a closed FsFile reports zero for these.
  EXPECT_EQ(file.size(), 0u);
  EXPECT_EQ(file.position(), 0u);
}

TEST_F(HalFileContract, MovedFromSourceHasNoImpl) {
  fake::add("/book", "bytes");
  HalFile source = Storage.open("/book");
  HalFile destination = std::move(source);
  EXPECT_TRUE(destination);
  EXPECT_FALSE(source.isOpen());
  EXPECT_FALSE(static_cast<bool>(source));
  EXPECT_DEATH(source.close(), kNoImpl);
  EXPECT_DEATH(readByte(source), kNoImpl);
  destination.close();
}

TEST_F(HalFileContract, MoveAssignmentInvalidatesTheSource) {
  fake::add("/book", "bytes");
  HalFile source = Storage.open("/book");
  HalFile destination;
  destination = std::move(source);
  EXPECT_FALSE(source.isOpen());
  EXPECT_FALSE(static_cast<bool>(source));
  destination.close();
  EXPECT_DEATH(readName(source), kNoImpl);
}

TEST_F(HalFileContract, ExhaustedDirectoryEntryStaysCloseable) {
  fake::add("/dir", "");
  fake::add("/dir/one", "x");
  HalFile directory = Storage.open("/dir");
  ASSERT_TRUE(directory);
  HalFile entry = directory.openNextFile();
  ASSERT_TRUE(entry);
  entry.close();
  HalFile exhausted = directory.openNextFile();
  ASSERT_FALSE(exhausted);
  exhausted.close();  // openNextFile always returns an entry handle with an impl.
  exhausted.close();
}
