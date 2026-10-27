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
word = between(header, '  struct WordBox {', '  static constexpr size_t MAX_SELECTABLE_WORDS')
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
  void prewarmWord(int) const;
  void ditherGapBetween(const WordBox&, const WordBox&, int, int) const;
  void drawWordHighlight(int, int, int, int, int) const;
};
''']
for first, last in [
    ('bool ClipSelectionActivity::extractWords()', 'int ClipSelectionActivity::closestInRow('),
    ('void ClipSelectionActivity::prewarmWord(', 'void ClipSelectionActivity::clearGapBetween('),
    ('void ClipSelectionActivity::drawWordHighlight(', 'bool ClipSelectionActivity::renderIncremental()'),
]:
    parts += [between(clip, first, last)]
output.write_text('\n'.join(parts))
