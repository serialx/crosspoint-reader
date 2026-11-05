#pragma once
#include <Epub/Page.h>

#include <cassert>
#include <cstring>

static void comparePage(const Page& native, const Page& borrowed) {
  assert(native.visibleTextOffset == borrowed.visibleTextOffset);
  assert(native.elementCount() == borrowed.elementCount());
  assert(native.footnoteCount() == borrowed.footnoteCount());
  assert(native.linkCount() == borrowed.linkCount());
  assert(native.hasImages() == borrowed.hasImages());
  int16_t ax = 0, ay = 0, aw = 0, ah = 0, bx = 0, by = 0, bw = 0, bh = 0;
  assert(native.getImageBoundingBox(ax, ay, aw, ah) == borrowed.getImageBoundingBox(bx, by, bw, bh));
  assert(ax == bx && ay == by && aw == bw && ah == bh);
  for (uint16_t i = 0; i < native.footnoteCount(); ++i) {
    FootnoteEntry a{}, b{};
    native.footnote(i, a);
    borrowed.footnote(i, b);
    assert(std::memcmp(&a, &b, sizeof(a)) == 0);
  }
  for (uint16_t i = 0; i < native.linkCount(); ++i) {
    PageLink a{}, b{};
    native.link(i, a);
    borrowed.link(i, b);
    assert(std::memcmp(&a, &b, sizeof(a)) == 0);
  }
  for (uint16_t i = 0; i < native.elementCount(); ++i) {
    const auto a = native.element(i), b = borrowed.element(i);
    assert(a.getTag() == b.getTag() && a.xPos == b.xPos && a.yPos == b.yPos);
    switch (a.getTag()) {
      case TAG_PageLine: {
        auto x = a.textBlock(), y = b.textBlock();
        assert(x.valid() == y.valid() && x.wordCount() == y.wordCount());
        assert(x.hasRuby() == y.hasRuby() && x.rubyCount() == y.rubyCount());
        const auto& s = x.getBlockStyle();
        const auto& t = y.getBlockStyle();
        assert(s.alignment == t.alignment && s.characterSpacing == t.characterSpacing && s.marginTop == t.marginTop &&
               s.marginBottom == t.marginBottom && s.marginLeft == t.marginLeft && s.marginRight == t.marginRight &&
               s.paddingTop == t.paddingTop && s.paddingBottom == t.paddingBottom && s.paddingLeft == t.paddingLeft &&
               s.paddingRight == t.paddingRight && s.textIndent == t.textIndent &&
               s.textIndentDefined == t.textIndentDefined && s.textAlignDefined == t.textAlignDefined &&
               s.isRtl == t.isRtl && s.directionDefined == t.directionDefined && s.fontId == t.fontId);
        for (uint16_t word = 0; word < x.wordCount(); ++word) {
          assert(x.wordTextLen(word) == y.wordTextLen(word));
          assert(std::strcmp(x.wordText(word), y.wordText(word)) == 0);
          assert(x.wordXpos(word) == y.wordXpos(word) && x.wordStyle(word) == y.wordStyle(word));
          assert(x.wordFontId(word, 24) == y.wordFontId(word, 24));
          assert(x.wordHasSmallCaps(word) == y.wordHasSmallCaps(word));
          assert(x.wordHasDiscretionaryHyphen(word) == y.wordHasDiscretionaryHyphen(word));
          assert(x.wordStartsParagraph(word) == y.wordStartsParagraph(word));
          assert(x.focusBoundary(word) == y.focusBoundary(word) && x.focusSuffixX(word) == y.focusSuffixX(word));
          assert(x.rubyText(word) == y.rubyText(word));
          const auto sa = x.wordSourceRange(word), sb = y.wordSourceRange(word);
          assert(sa.start == sb.start && sa.end == sb.end);
        }
        break;
      }
      case TAG_PageImage: {
        auto x = a.imageBlock(), y = b.imageBlock();
        assert(x.getImagePath() == y.getImagePath());
        assert(x.getWidth() == y.getWidth() && x.getHeight() == y.getHeight());
        break;
      }
      case TAG_PageDropCap:
        assert(a.dropCapText() == b.dropCapText());
        break;
      case TAG_PageHorizontalRule:
      case TAG_PageBorderBox:
        break;
    }
  }
}
