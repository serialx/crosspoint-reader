#pragma once
#include <xp_ui_reader.h>
struct ChapterPosition {
  int pageIndex = 0;
  int totalPages = 0;
  int displayPage() const { return xp_ui_reader_chapter_display(pageIndex, totalPages).display_page; }
  bool hasTotal() const { return xp_ui_reader_chapter_display(pageIndex, totalPages).has_total; }
  float chapterFraction() const { return xp_ui_reader_chapter_display(pageIndex, totalPages).fraction; }
};
