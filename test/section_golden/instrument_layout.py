#!/usr/bin/env python3
"""Instrument a host-only copy; fail if a trace anchor changed or is ambiguous."""
import sys
from pathlib import Path


def instrument(source):
    def before(anchor, code):
        nonlocal source
        if source.count(anchor) != 1:
            raise ValueError(f"Layout trace anchor must occur once: {anchor!r}")
        source = source.replace(anchor, code + "\n" + anchor)

    source = '#include "LayoutTrace.h"\n' + source
    before('  const int pageWidth = viewportWidth;', '''
  LayoutTrace::record("paragraph", viewportWidth, fontId, includeLastLine,
                      static_cast<int>(characterSpacing), static_cast<int>(wordSpacingPercent),
                      maxLines, firstLineConsumed, paragraphStartEmitted, blockStyle.isRtl);
  for (size_t i = 0; i < tokenCount(); ++i) {
    const auto token = tokenAt(i);
    LayoutTrace::record("token", i, LayoutTrace::hex(wordAt(i)), visibleOffsetAt(i),
                        int(token.style), int(token.flags), int(token.focus), int(token.slot),
                        int(token.link), wordFontId(i, fontId));
  }''')
    before('  const bool endIsKnown = includeLastLine && allLinesComputed;', '''
  LayoutTrace::record("breaks", lineCount, allLinesComputed);
  for (size_t i = 0; i < lineBreakIndices.size(); ++i)
    LayoutTrace::record("break", i, lineBreakIndices[i]);
  for (size_t i = 0; i < wordWidths.size(); ++i)
    LayoutTrace::record("width", i, wordWidths[i]);''')
    before('  XpLayoutSplitResult split{};', '  const std::string traceSplitWord{wordAt(wordIndex)};')
    before('  return splitSucceeded;', '''
  if (splitSucceeded)
    LayoutTrace::record("split", wordIndex, availableWidth, allowFallbackBreaks,
                        split.offset, int(split.hyphen), split.width, split.remainder_width,
                        LayoutTrace::hex(traceSplitWord), split.remainder_offset);
''')
    before('  wordWidths.insert(wordWidths.begin() + wordIndex + 1, remainderWidth);', '''
  LayoutTrace::record("split", wordIndex, availableWidth, allowFallbackBreaks,
                      chosenOffset, chosenNeedsHyphen, chosenWidth, remainderWidth,
                      LayoutTrace::hex(word), remainderOffset);''')
    before('  const auto focusBoundaryAt = [&](const size_t idx) {', '''
  LayoutTrace::record("line", breakIndex, lastBreakAt, lineBreak, lineVisibleOffset,
                      firstLineIndent, extraStartOffset, extraEndOffset, lineWordWidthSum,
                      actualGapCount, totalNaturalGaps, spareSpace, justifyExtra, willReorder);
  for (size_t i = 0; i < lineWordCount; ++i)
    LayoutTrace::record("visual", i, willReorder ? visualOrderScratch[i] : i, lineXPos[i]);''')
    before('    const int justifyContribution = (effectiveAlignment == CssTextAlign::Justify && !isLastLine)', '''
    LayoutTrace::record("reordered-gaps", reorderedWordWidthSum, reorderedNaturalGaps,
                        reorderedGapCount, reorderedSpare, reorderedJustifyExtra);''')
    before('  if (!lineHasFocusSplit) {', '''
  for (size_t i = 0; i < lineWordCount; ++i)
    LayoutTrace::record("output", i, LayoutTrace::hex(lineWords[i]), lineXPos[i],
                        int(lineWordStyles[i]), int(focusBoundaryAt(i)),
                        sourceRanges[i].start, sourceRanges[i].end, LayoutTrace::hex(lineRubyTexts[i]));
  for (const auto& link : lineLinks)
    LayoutTrace::record("link", LayoutTrace::hex(link.href), link.x, link.width, link.topLift);
  LayoutTrace::record("paragraph-start-word", paragraphStartWord);''')
    return source


if __name__ == '__main__':
    Path(sys.argv[2]).write_text(instrument(Path(sys.argv[1]).read_text()))
