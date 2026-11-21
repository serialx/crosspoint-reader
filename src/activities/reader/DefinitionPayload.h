#pragma once
#include <EpdFontFamily.h>
#include <Epub/Page.h>
#include <xp_epub_session.h>

#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "util/DictionaryBuffer.h"
class GfxRenderer;
class MappedInputManager;
struct XpDefinitionNativeTransfer {
  enum class TextKind : uint8_t { Copy, Move };
  const std::string* title;
  union Text {
    const std::string* copy;
    std::string* move;
  } text;
  TextKind kind;
  bool html;
};
// Native construction/raster resources. Navigation and publication belong to Rust.
class DefinitionPayload {
 public:
  struct Line {
    uint32_t start;
    uint16_t len;
    bool bold;
  };
  struct Slot {
    std::string headword;
    std::variant<std::string, DictionaryBuffer> text;
    std::string_view textView() const {
      if (const auto* buffer = std::get_if<DictionaryBuffer>(&text)) return buffer->view();
      return std::get<std::string>(text);
    }
    std::vector<std::unique_ptr<Page>> pages;
    std::vector<Line> lines;
    int linesPerPage = 1;
    int totalPages() const {
      if (!pages.empty()) return static_cast<int>(pages.size());
      return lines.empty() ? 1 : 1 + static_cast<int>((lines.size() - 1) / linesPerPage);
    }
    void releasePayload();
  };
  Slot slots[2];
  std::vector<std::unique_ptr<Page>> selectionPages;
  static uint8_t constructPlain(XpDefinitionPayloadStorage*, XpAppText, uint32_t, XpEpubDefinitionFill, void*);
  static uint8_t constructNative(XpDefinitionPayloadStorage*, XpDefinitionNativeTransfer*, uint8_t*);
  static DefinitionPayload& get(XpDefinitionPayloadStorage* storage);
  static const DefinitionPayload& get(const XpDefinitionPayloadStorage* storage);
  Slot& slot(uint8_t index);
  const Slot& slot(uint8_t index) const;
  bool prepareDefinition(GfxRenderer& renderer, Slot& next, bool htmlDefinition, bool forcePlain, XpUiRect body);
  bool buildPlainSelectionPages(GfxRenderer& renderer, int selectionPage);
  void* selectionChild(GfxRenderer& renderer, MappedInputManager& input, const XpDefinitionSelectionRequest& request,
                       XpUiRect body);
  void drawBody(GfxRenderer& renderer, int fontId, int x, int startY, int currentPage) const;
  void drawHeader(GfxRenderer& renderer, XpUiRect bounds, int page, int pages) const;

 private:
  struct PlainSelectionLine;
  static bool layoutHtmlPages(GfxRenderer& renderer, Slot& next, XpUiRect body);
  static bool wrapText(GfxRenderer& renderer, Slot& next, XpUiRect body);
  static bool reserveLines(Slot& next, size_t count);
  static int measureSpan(const GfxRenderer& renderer, int fontId, const char* text, size_t len,
                         EpdFontFamily::Style family = EpdFontFamily::REGULAR);
};
