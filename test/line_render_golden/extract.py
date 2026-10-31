"""Compile production consumer methods with host-only activity ownership stubs."""
import pathlib
import sys
root, output = map(pathlib.Path, sys.argv[1:])
dictionary = (root / 'src/activities/reader/DictionaryDefinitionActivity.cpp').read_text()
clip = (root / 'src/activities/reader/ClipSelectionActivity.cpp').read_text()
header = (root / 'src/activities/reader/ClipSelectionActivity.h').read_text()
def between(text, first, last):
    assert text.count(first) == 1 and text.count(last) == 1, (first, last)
    return text[text.index(first):text.index(last)]
parts = [between(dictionary, 'namespace {', '// cppcheck-suppress constParameterReference')]
parts += [between(dictionary, 'int DictionaryDefinitionActivity::measureSpan(',
                  'bool DictionaryDefinitionActivity::reserveLines(')]
parts += [between(dictionary, 'bool DictionaryDefinitionActivity::buildPlainSelectionPages()',
                  'void DictionaryDefinitionActivity::startSelection(')]
word = between(header, '  using WordBox =', '  static constexpr size_t MAX_SELECTABLE_WORDS')
parts += ['struct ClipSelectionActivity {\n' + word + '''
  enum class Mode { Clip, DefinitionLookup };
  static constexpr size_t MAX_SELECTABLE_WORDS = 240;
  GfxRenderer& renderer;
  std::span<const std::unique_ptr<Page>> pages;
  Mode mode = Mode::DefinitionLookup;
  int fontId, marginLeft = 12, marginTop = 12;
  uint8_t currentPageOffset = 0;
  uint16_t rowCount = 0;
  size_t wordCount = 0;
  std::unique_ptr<WordBox[]> words;
  size_t pageCount() const { return pages.size(); }
  const Page& pageAt(size_t i) const { return *pages[i]; }
  bool extractWords();
  void drawSelection(int first, int last);
};
''']
parts += [between(clip, 'bool ClipSelectionActivity::extractWords()', 'bool ClipSelectionActivity::buildSelectedText(')]
start = clip.index('[](void* context, const XpGlyphTarget*, const XpUiSelectionWord* word')
end = clip.index('}};', start) + 1
callback = clip[start:end]
parts += ['void ClipSelectionActivity::drawSelection(int first, int last) {\n' + r'''
  struct Context { ClipSelectionActivity* self; bool incremental; } context{this, true};
  const XpUiSelectionFonts fonts{&context, ''' + callback + r'''};
  XpUiSelectionState state{};
  xp_ui_selection_init(&state, 0, words.get(), wordCount, 32767, first, last);
  state.page = currentPageOffset;
  XpUiSelectionLayout layout{};
  layout.screen = layout.safe = {0, 0, 32767, 32767};
  layout.touch = 1;
  XpUiSelectionPainted old{};
  const auto clip = renderer.getClipRect();
  const XpGlyphTarget target{renderer.getWriteTarget(), HalDisplay::BUFFER_SIZE,
      HalDisplay::DISPLAY_WIDTH, HalDisplay::DISPLAY_HEIGHT, HalDisplay::DISPLAY_WIDTH_BYTES,
      0, HalDisplay::DISPLAY_HEIGHT, {clip[0], clip[1], clip[0] + clip[2], clip[1] + clip[3]},
      static_cast<uint8_t>(renderer.getOrientation()), static_cast<uint8_t>(renderer.getRenderMode()), 0, 0, 0};
  if (!xp_ui_selection_paint(&state, words.get(), wordCount, &layout, &old, &target, &fonts, 0)) abort();
}
''']
output.write_text('\n'.join(parts))
