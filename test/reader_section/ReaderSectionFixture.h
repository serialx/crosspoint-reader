#pragma once
#include <Epub/Page.h>
#include <Epub/ReaderRenderSpec.h>
#include <xp_epub_session.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
class GfxRenderer;

// Input metadata only. The bridge serializes it and loads an actual Rust ReaderBook.
struct ReaderSectionInput {
  struct Spine {
    std::string href;
  };
  struct Toc {
    int spineIndex;
    std::string anchor;
  };
  std::vector<Spine> spine;
  std::vector<Toc> toc;
  std::string language;
};

class ReaderSectionFixture {
  void* owner = nullptr;
  std::function<void()> popup;
  explicit ReaderSectionFixture(void* handle);
  static void* open(const ReaderSectionInput&, int, GfxRenderer&);
  void refresh();
  std::optional<uint32_t> query(uint8_t kind, uint32_t value = 0, bool first = false) const;
  std::optional<uint16_t> anchor(const std::string& value, uint8_t kind) const;
  static XpEpubRenderSpec spec(const ReaderRenderSpec& value);
  static void showPopup(void* context);

 public:
  uint16_t pageCount = 0;
  int currentPage = 0;
  ReaderSectionFixture(const std::shared_ptr<ReaderSectionInput>& input, int spine, GfxRenderer& renderer);
  static std::unique_ptr<ReaderSectionFixture> tryOpen(const ReaderSectionInput&, int, GfxRenderer&);
  ~ReaderSectionFixture();
  ReaderSectionFixture(const ReaderSectionFixture&) = delete;
  ReaderSectionFixture& operator=(const ReaderSectionFixture&) = delete;
  bool loadSectionFile(const ReaderRenderSpec& value);
  bool createSectionFile(const ReaderRenderSpec& value, const std::function<void()>& callback = {});
  bool startBuild(const ReaderRenderSpec& value, const std::function<void()>& callback = {});
  bool buildSomeMore(int maxPages, const std::function<bool()>& yielding = {});
  void suspendBuild();
  void abandonBuild();
  bool clearCache();
  bool hasHtmlCache() const;
  bool isBuilding() const;
  bool isBuildComplete() const;
  bool isPartial() const;
  uint16_t estimatedTotalPages() const;
  std::unique_ptr<Page> loadPage(int index) const;
  std::optional<uint16_t> getCachedPageCount() const;
  std::optional<uint16_t> getPageForParagraphIndex(uint16_t index) const;
  std::optional<uint16_t> getPageForListItemIndex(uint16_t index) const;
  std::optional<uint16_t> getParagraphIndexForPage(uint16_t index) const;
  std::optional<uint32_t> getVisibleTextOffsetForPage(uint16_t index) const;
  std::optional<uint16_t> getPageForVisibleTextOffset(uint32_t offset, bool first = false) const;
  bool buildReachedVisibleTextOffset(uint32_t offset) const;
  std::optional<uint16_t> findAnchor(const std::string& value) const;
  std::optional<uint16_t> getPageForAnchor(const std::string& value) const;
  std::optional<uint16_t> findAnchorDuringBuild(const std::string& value) const;
  int getTocIndexForPage(int page) const;
  std::string getTextFromSectionFile() const;
};
