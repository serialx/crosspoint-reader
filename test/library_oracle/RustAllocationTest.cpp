#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>

#include "Epub.h"
#include "LibraryBuilder.h"
#include "LibraryIndexFile.h"

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
