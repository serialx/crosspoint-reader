#!/usr/bin/env python3
"""Record the unsplit C++ boundary in a host-only generated translation unit."""
import re
import sys
from pathlib import Path


def instrument(source):
    paragraph_names = {
        'isEmpty': 'paragraphEmpty', 'size': 'paragraphSize', 'getBlockStyle': 'paragraphStyle',
        'setBlockStyle': 'setParagraphStyle', 'suppressFirstLineIndent': 'suppressFirstLineIndent',
        'ensureRubyCapacity': 'ensureRubyCapacity', 'addLinkTarget': 'addLinkTarget',
        'linkTargetMatches': 'linkTargetMatches', 'sizeSlotFor': 'sizeSlotFor',
        'getWordStyleAt': 'getWordStyleAt', 'getRubyTextAt': 'getRubyTextAt',
        'setRubyGroupAt': 'setRubyGroupAt', 'setRubyForWordAt': 'setRubyForWordAt', 'addWord': 'addWord',
    }

    def semantic_calls(value):
        for old, new in paragraph_names.items():
            value = value.replace('currentTextBlock->' + old + '(', new + '(')
        return value.replace('self->pendingFootnotes.push_back({wordIndex, entry});',
                             'self->addPendingFootnote(wordIndex, entry);')

    def resolve(anchor):
        for candidate in (anchor,
                          anchor.replace('ChapterHtmlSlimParser::', 'ChapterSemantics::'),
                          anchor.replace('ChapterHtmlSlimParser::', 'ChapterPageAssembler::'),
                          semantic_calls(anchor)):
            pattern = r"\s+".join(re.escape(part) for part in candidate.split())
            matches = list(re.finditer(pattern, source))
            if len(matches) > 1:
                raise ValueError(f"Ambiguous chapter trace anchor: {candidate!r}")
            if matches:
                return matches[0].group()
        return anchor

    def before(anchor, code):
        nonlocal source
        resolved = resolve(anchor)
        if semantic_calls(anchor).split() == resolved.split() and anchor.split() != resolved.split():
            code = semantic_calls(code)
        if source.count(resolved) != 1:
            raise ValueError(f"Chapter trace anchor must occur once: {resolved!r}")
        source = source.replace(resolved, code + '\n' + resolved)

    source = '#include "ChapterTrace.h"\n' + source
    # Record service calls as expressions, including their replies, exactly once.
    pattern = r'(?P<receiver>(?:self->)?renderer)\.(?P<method>getFontAscenderSize|getLineHeight|getSpaceWidth|getTextAdvanceX|resolveFontVariant|isSdCardFont)\('
    matches = list(re.finditer(pattern, source))
    for match in reversed(matches):
        start = match.end()
        end = start
        depth = 1
        while depth:
            if source[end] == '(':
                depth += 1
            elif source[end] == ')':
                depth -= 1
            end += 1
        call = source[match.start():end]
        args = source[start:end-1]
        # Text arguments are byte strings rather than pointer addresses.
        logged = args.replace('word, style', 'ChapterTrace::hex(word), style')
        replacement = ('([&]() { const auto reply = ' + call + '; ChapterTrace::record("' +
                       match['method'] + '", ' + logged + ', reply); return reply; }())')
        source = source[:match.start()] + replacement + source[end:]

    hooks = {
        'void ChapterHtmlSlimParser::startNewTextBlock(const BlockStyle& blockStyle, const bool paragraphEnd) {':
            'ChapterTrace::record("BeginBlock", blockStyle, paragraphEnd, xpathParagraphIndex, xpathListItemIndex);',
        'void ChapterHtmlSlimParser::makePages(const bool includeLastLine, const bool paragraphEnd) {':
            'ChapterTrace::record("EndBlock", includeLastLine, paragraphEnd, visibleTextOffset);',
        'void ChapterHtmlSlimParser::breakTextLine(const BlockStyle& style) {':
            'ChapterTrace::record("LineBreak", style, effectivePreserveWhitespace);',
        'void ChapterHtmlSlimParser::emitHorizontalRule(const BlockStyle& blockStyle) {':
            'ChapterTrace::record("HorizontalRule", blockStyle, visibleTextOffset);',
        'void ChapterHtmlSlimParser::applyPendingPageBreak() {':
            'ChapterTrace::record("PageBreak", pendingPageBreak, currentTextBlock && currentTextBlock->getBlockStyle().pageBreakBefore);',
        'void ChapterHtmlSlimParser::flushPendingAnchor() {':
            'ChapterTrace::record("Anchor", ChapterTrace::hex(pendingAnchorId), completedPageCount, stopRequested);',
        'void ChapterHtmlSlimParser::closeBoxScope() {':
            'ChapterTrace::record("BoxClose", depth);',
        'void ChapterHtmlSlimParser::cancelDropCapToWord() {':
            'ChapterTrace::record("DropCapCancel", ChapterTrace::hex(std::string_view(dropCap.text, dropCap.length)));',
        'void ChapterHtmlSlimParser::fallbackTableRowToStacked() {':
            'ChapterTrace::record("TableFallback", tableRowStacked);',
        'void ChapterHtmlSlimParser::closeTableCell() {':
            'ChapterTrace::record("TableCellEnd", insideTableCell, tableRowStacked);',
        'void ChapterHtmlSlimParser::finishTableRow() {':
            'ChapterTrace::record("TableRowEnd", tableRowRtl, tableRowStacked, tableColumnCount);',
        'void ChapterHtmlSlimParser::setCurrentPageVisibleOffset(const uint32_t offset) {':
            'ChapterTrace::record("SetVisibleOffset", offset);',
        'void ChapterHtmlSlimParser::requestStop() {':
            'ChapterTrace::record("StopRequested");',
        'bool ChapterHtmlSlimParser::beginParse() {':
            'ChapterTrace::record("Begin", fontId, lineCompression, extraParagraphSpacing, paragraphIndentSpaces, characterSpacing, wordSpacingPercent, paragraphAlignment, viewportWidth, viewportHeight, hyphenationEnabled, focusReadingEnabled, embeddedStyle, imageRendering);',
        'ChapterHtmlSlimParser::ParseStatus ChapterHtmlSlimParser::parseStep() {':
            'ChapterTrace::record("Step", parseFile_.position());',
        'bool ChapterHtmlSlimParser::finishParse() {':
            'ChapterTrace::record("Finish", stopRequested, layoutOom);',
    }
    for anchor, code in hooks.items():
        anchor = resolve(anchor)
        if source.count(anchor) != 1:
            raise ValueError(f"Chapter trace method must occur once: {anchor!r}")
        source = source.replace(anchor, anchor + '\n  ' + code)
    before('  currentTextBlock->addWord(partWordBuffer, fontStyle, false, nextWordContinues, partWordVisibleOffset, linkId,', '''
  ChapterTrace::record("Text", ChapterTrace::hex(std::string_view(partWordBuffer, wordBytes)), fontStyle,
      nextWordContinues, partWordVisibleOffset, linkId, sizeSlot, effectivePreserveWhitespace,
      inlineScale, currentTextBlock->getBlockStyle());''')
    before('  boxScopes.push_back(box);', '''
  ChapterTrace::record("BoxOpen", box.depth, box.left, box.right, box.padTop, box.padBottom,
      box.sides[0], box.sides[1], box.sides[2], box.sides[3], box.shaded, ownStyle);''')
    before('  dropCap.firstLetterPending = true;', '  ChapterTrace::record("DropCapArm", lines, firstLetter.fontWeight);')
    before('  memcpy(dropCap.text + dropCap.length, bytes, static_cast<size_t>(length));',
           '  ChapterTrace::record("DropCapCapture", ChapterTrace::hex(std::string_view(bytes, length)), dropCap.lines, dropCap.bold);')
    before('  tableColumnCount = static_cast<uint8_t>(columns);', '''
  ChapterTrace::record("TableColumns", columns);
  for (int column = 0; column < columns; ++column)
    ChapterTrace::record("Column", column, measure.minWidth[column], measure.prefWidth[column], tableColumnWidths[column]);''')
    before('      self->pendingFootnotes.push_back({wordIndex, entry});',
           '      ChapterTrace::record("Footnote", wordIndex, ChapterTrace::hex(entry.number), ChapterTrace::hex(entry.href));')
    before('          self->currentTextBlock->addWord(marker, EpdFontFamily::REGULAR, false, false, self->visibleTextOffset);',
           '          ChapterTrace::record("ListMarker", ChapterTrace::hex(marker), self->visibleTextOffset);')
    before('          self->currentTextBlock->addWord("\\xe2\\x80\\xa2", EpdFontFamily::REGULAR, false, false,',
           '          ChapterTrace::record("ListMarker", "e280a2", self->visibleTextOffset);')
    before('          self->currentTextBlock->setRubyGroupAt(self->rubyStartWordIndex, baseWordCount, cleanRuby);',
           '          ChapterTrace::record("RubyGroup", self->rubyStartWordIndex, baseWordCount, ChapterTrace::hex(cleanRuby));')
    before('            self->currentTextBlock->setRubyForWordAt(leaderIdx, prevRuby + cleanRuby);',
           '            ChapterTrace::record("RubyAppend", leaderIdx, ChapterTrace::hex(prevRuby + cleanRuby));')
    before('      if (!src.empty() && self->imageRendering != 1) {', '''
      ChapterTrace::record("Image", ChapterTrace::hex(src), ChapterTrace::hex(alt), cssStyle.imageWidth,
          cssStyle.imageHeight, cssStyle.hasImageWidth(), cssStyle.hasImageHeight());''')
    before('              if (gotDimensions) {',
           '              ChapterTrace::record("ImageReply", gotDimensions, dims.width, dims.height);')
    before('                // Flush any pending text block so it appears before the image',
           '                ChapterTrace::record("ImageSize", displayWidth, displayHeight);')
    before('  completePageFn(std::move(currentPage), xpathParagraphIndex, xpathListItemIndex, currentPageVisibleOffset);',
           '  ChapterTrace::record("Page", xpathParagraphIndex, xpathListItemIndex, currentPageVisibleOffset, currentPageNextY);')
    # Paragraph services used by semantic callbacks, including opaque slot/link replies.
    reverse_names = {new: old for old, new in paragraph_names.items()}
    services = '|'.join(reverse_names)
    pattern = r'(?:(?:self->)?currentTextBlock->(?P<method>\w+)|(?<![\w>:])(?:self->)?(?P<service>' + services + r'))\('
    matches = list(re.finditer(pattern, source))
    for match in reversed(matches):
        if match['method'] == 'layoutAndExtractLines':
            continue
        start = match.end()
        end = start
        depth = 1
        quote = None
        escape = False
        while depth:
            char = source[end]
            if quote:
                if escape:
                    escape = False
                elif char == "\\":
                    escape = True
                elif char == quote:
                    quote = None
            elif char in ('"', "'"):
                quote = char
            elif char == '(':
                depth += 1
            elif char == ')':
                depth -= 1
            end += 1
        call = source[match.start():end]
        args = source[start:end-1]
        replacement = ('ChapterTrace::call("Paragraph.' + (match['method'] or reverse_names[match['service']]) +
                       '", [&]() -> decltype(auto) { return ' + call + '; }' +
                       (', ' + args if args.strip() else '') + ')')
        source = source[:match.start()] + replacement + source[end:]

    return source


if __name__ == '__main__':
    source = Path(sys.argv[1]).read_text()
    for extra in sys.argv[3:]:
        source += '\n' + Path(extra).read_text()
    Path(sys.argv[2]).write_text(instrument(source))
