#pragma once
#include <Epub/Page.h>
#include <Logging.h>
#include <Memory.h>

// Frozen native reader metadata oracle from deafbe10; never included by firmware.
// Reader-owned copies outlive the rendered Page; failed assignment leaves no stale targets.
class PageMetadata {
  std::unique_ptr<FootnoteEntry[]> ownedFootnotes;
  std::unique_ptr<PageLink[]> ownedLinks;
  uint16_t footnoteSize = 0;
  uint16_t linkSize = 0;

 public:
  std::span<const FootnoteEntry> footnotes() const { return {ownedFootnotes.get(), footnoteSize}; }
  std::span<const PageLink> links() const { return {ownedLinks.get(), linkSize}; }
  void clear();
  bool assign(const Page& page);
};

inline void PageMetadata::clear() {
  ownedFootnotes.reset();
  ownedLinks.reset();
  footnoteSize = linkSize = 0;
}

inline bool PageMetadata::assign(const Page& page) {
  clear();
  const auto notes = page.footnoteCount();
  const auto links = page.linkCount();
  auto newNotes = notes ? makeUniqueNoThrow<FootnoteEntry[]>(notes) : nullptr;
  if (notes && !newNotes) {
    LOG_ERR("PGE", "OOM: footnotes %u bytes", static_cast<unsigned>(notes * sizeof(FootnoteEntry)));
    return false;
  }
  auto newLinks = links ? makeUniqueNoThrow<PageLink[]>(links) : nullptr;
  if (links && !newLinks) {
    LOG_ERR("PGE", "OOM: links %u bytes", static_cast<unsigned>(links * sizeof(PageLink)));
    return false;
  }
  for (uint16_t i = 0; i < notes; ++i) page.footnote(i, newNotes[i]);
  for (uint16_t i = 0; i < links; ++i) page.link(i, newLinks[i]);
  ownedFootnotes = std::move(newNotes);
  ownedLinks = std::move(newLinks);
  footnoteSize = notes;
  linkSize = links;
  return true;
}
