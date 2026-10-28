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
  for (size_t fail = 0; fail < 160; ++fail) {
    SCOPED_TRACE(fail);
    xp_test_fail_allocation_after(SIZE_MAX);
    fake::reset();
    bookMetadata.clear();
    fake::add("/a.epub");
    bookMetadata["/a.epub"] = {std::string(200, 'a'), std::string(120, 'b'), true};
    library::BuildStats stats;
    ASSERT_TRUE(library::buildLibraryIndex("/", stats, true));
    const auto before = fake::files[library::libraryIndexPath()]->bytes;
    fake::add("/b.epub", "new book");
    bookMetadata["/b.epub"] = {std::string(200, 'c'), std::string(120, 'd'), true};
    xp_test_fail_allocation_after(fail);
    const bool ok = library::buildLibraryIndex("/", stats, true);
    xp_test_fail_allocation_after(SIZE_MAX);
    EXPECT_EQ(xp_test_live_allocations(), 0u);
    if (!ok) EXPECT_EQ(fake::files[library::libraryIndexPath()]->bytes, before);
    library::LibraryIndexFile index;
    ASSERT_TRUE(index.open(library::libraryIndexPath()));
    EXPECT_EQ(index.bookCount(), ok ? 2u : 1u);
  }
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
