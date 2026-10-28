#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "Epub.h"
#include "LibraryBuilder.h"
#include "LibraryIndexFile.h"
#include "xp_ffi.h"

extern "C" void xp_test_fail_allocation_after(size_t count);
extern "C" size_t xp_test_live_allocations();

TEST(LibraryRustAllocation, ScratchFailuresKeepThePreviousIndexAndReleaseStorage) {
  for (const auto& author : {std::string(), std::string(120, 'b')}) {
    SCOPED_TRACE(author.size());
    std::vector<uint8_t> expected;
    for (size_t fail = 0; fail <= 160; ++fail) {
      SCOPED_TRACE(fail);
      xp_test_fail_allocation_after(SIZE_MAX);
      fake::reset();
      bookMetadata.clear();
      fake::add("/a.epub");
      bookMetadata["/a.epub"] = {std::string(200, 'a'), author, true};
      library::BuildStats stats;
      ASSERT_TRUE(library::buildLibraryIndex("/", stats, true));
      const auto before = fake::files[library::libraryIndexPath()]->bytes;
      if (author.empty()) {
        fake::add("/b.txt", "new book");
      } else {
        fake::add("/b.epub", "new book");
        bookMetadata["/b.epub"] = {std::string(200, 'c'), std::string(120, 'd'), true};
      }
      ASSERT_TRUE(library::markLibraryIndexDirty());
      // First capture a complete successful rebuild, then inject each failure position.
      xp_test_fail_allocation_after(fail == 0 ? SIZE_MAX : fail - 1);
      const bool ok = library::buildLibraryIndex("/", stats, true);
      xp_test_fail_allocation_after(SIZE_MAX);
      EXPECT_EQ(xp_test_live_allocations(), 0u);
      if (fail == 0) {
        ASSERT_TRUE(ok);
        expected = fake::files[library::libraryIndexPath()]->bytes;
      }
      EXPECT_EQ(fake::files[library::libraryIndexPath()]->bytes, ok ? expected : before);
      EXPECT_EQ(library::isLibraryIndexDirty(), !ok);
      library::LibraryIndexFile index;
      ASSERT_TRUE(index.open(library::libraryIndexPath()));
      EXPECT_EQ(index.bookCount(), ok ? 2u : 1u);
    }
  }
}

TEST(LibraryRustAllocation, CachedTitleAllocationFailureKeepsMetadataAndDirtyState) {
  fake::reset();
  bookMetadata.clear();
  fake::add("/a.epub", "book", 7);
  const std::string title(200, 'a');
  bookMetadata["/a.epub"] = {title, "", true};
  library::BuildStats stats;
  ASSERT_TRUE(library::buildLibraryIndex("/", stats, true));
  const auto before = fake::files[library::libraryIndexPath()]->bytes;
  fake::add("/b.txt");
  ASSERT_TRUE(library::markLibraryIndexDirty());
  xp_test_fail_allocation_after(0);
  const bool ok = library::buildLibraryIndex("/", stats, true);
  xp_test_fail_allocation_after(SIZE_MAX);
  EXPECT_FALSE(ok);
  EXPECT_FALSE(stats.indexReplaced);
  EXPECT_EQ(fake::files[library::libraryIndexPath()]->bytes, before);
  EXPECT_TRUE(library::isLibraryIndexDirty());
  EXPECT_EQ(xp_test_live_allocations(), 0u);
  ASSERT_TRUE(library::buildLibraryIndex("/", stats, true));
  EXPECT_EQ(stats.metadataReused, 1u);
  EXPECT_FALSE(library::isLibraryIndexDirty());
  EXPECT_EQ(xp_test_live_allocations(), 0u);
  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open(library::libraryIndexPath()));
  ASSERT_EQ(index.bookCount(), 2u);
  library::ClixRecord record;
  ASSERT_TRUE(index.readRecord(0, record));
  std::string actual;
  ASSERT_TRUE(index.readTitle(record, actual));
  EXPECT_EQ(actual, title);
  EXPECT_FALSE(index.readAuthor(record, actual));
  EXPECT_TRUE(actual.empty());
}

TEST(LibraryRustAllocation, EmptyCachedTitleReusesWithoutAllocation) {
  fake::reset();
  bookMetadata.clear();
  fake::add("/a.epub", "book", 7);
  bookMetadata["/a.epub"] = {"", "", true};
  library::BuildStats stats;
  ASSERT_TRUE(library::buildLibraryIndex("/", stats, true));
  fake::add("/b.txt");
  xp_test_fail_allocation_after(0);
  const bool ok = library::buildLibraryIndex("/", stats, true);
  xp_test_fail_allocation_after(SIZE_MAX);
  ASSERT_TRUE(ok);
  EXPECT_EQ(stats.metadataReused, 1u);
  EXPECT_EQ(xp_test_live_allocations(), 0u);
  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open(library::libraryIndexPath()));
  EXPECT_EQ(index.bookCount(), 2u);
  library::ClixRecord record;
  ASSERT_TRUE(index.readRecord(0, record));
  std::string title;
  EXPECT_FALSE(index.readTitle(record, title));
  EXPECT_TRUE(title.empty());
}

TEST(LibraryRustAllocation, CorruptStagingAuthorCannotPanicOrReplaceTheIndex) {
  fake::reset();
  bookMetadata.clear();
  fake::add("/a.epub");
  library::BuildStats stats;
  ASSERT_TRUE(library::buildLibraryIndex("/", stats, true));
  const auto before = fake::files[library::libraryIndexPath()]->bytes;
  fake::add("/b.epub", "new book");
  fake::corruptStageAuthor = true;
  EXPECT_FALSE(library::buildLibraryIndex("/", stats, true));
  EXPECT_EQ(fake::files[library::libraryIndexPath()]->bytes, before);
  fake::corruptStageAuthor = false;
}
TEST(LibraryRustAllocation, InvalidSortOrdersNeverAliasAnExistingOrder) {
  fake::reset();
  bookMetadata.clear();
  fake::add("/a.epub");
  library::BuildStats stats;
  ASSERT_TRUE(library::buildLibraryIndex("/", stats, true));
  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open(library::libraryIndexPath()));
  for (unsigned order = 6; order < 256; ++order) {
    EXPECT_EQ(index.ordinalForRow(static_cast<library::SortOrder>(order), 0), 0xFFFF);
  }
}

TEST(LibraryRustAllocation, PackedAbiBuffersNeedNoAlignment) {
  fake::reset();
  bookMetadata.clear();
  fake::add("/a.epub");
  library::BuildStats stats;
  ASSERT_TRUE(library::buildLibraryIndex("/", stats, true));
  auto& bytes = fake::files[library::libraryIndexPath()]->bytes;
  alignas(4) uint8_t header[65]{};
  alignas(4) uint8_t record[129]{};
  std::memcpy(header + 1, bytes.data(), 64);
  ASSERT_EQ(xp_library_validate(header + 1, bytes.size(), 0), 0);
  const auto offset = xp_library_offset(header + 1, 0, 0);
  XpLibraryIndexIo io{
      &bytes,
      [](void* context, uint32_t at, uint8_t* out, size_t len) -> uint8_t {
        const auto& source = *static_cast<const std::vector<uint8_t>*>(context);
        if (at > source.size() || len > source.size() - at) return 0;
        std::memcpy(out, source.data() + at, len);
        return 1;
      },
      [](void*, uint32_t, size_t, uint8_t, uint8_t*) -> uint8_t { return 0; },
      [](void*, const uint8_t*, size_t) {},
      [](void*) {},
  };
  ASSERT_EQ(xp_library_read(header + 1, &io, record + 1, 0, 0, nullptr), 1u);
  ASSERT_LE(offset + 128u, bytes.size());
  EXPECT_EQ(std::memcmp(record + 1, bytes.data() + offset, 128), 0);
}
