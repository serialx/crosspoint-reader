#pragma once
#include "xp_ui.h"
#include "xp_ui_reader_chrome.h"

#ifdef __cplusplus
extern "C" {
#endif

// Private Home/Library/File Browser family. Strings are borrowed through paint;
// scene construction and resource callbacks run under the existing render lock.
typedef struct {
  int32_t selected;
  uint32_t last_repeat;
  uint8_t books, grid, opds, plugins, continuing, first_render, covers_loaded, loading;
} XpUiHomeState;
typedef struct {
  XpUiRect cover, menu;
  int16_t menu_step;
  uint8_t columns, continue_in_menu;
} XpUiHomeTouch;
typedef struct {
  XpUiEffect effect;
  uint8_t haptic, reserved[3];
} XpUiHomeUpdate;
typedef struct {
  XpUiRect screen, content, safe;
  int32_t title_font, small_font, tiny_font;
  int16_t top_padding, header_height, home_top, tile_height, cover_height;
  int16_t side_padding, spacing, menu_top, menu_height, menu_gap, hints_height, scroll_width, scroll_offset;
  uint8_t theme, continue_in_menu, columns, reserved;
} XpUiHomeLayout;
typedef struct {
  int32_t width, height;
  uint8_t status, reserved[3];  // 0 empty/invalid, 1 open failed, 2 parsed.
} XpUiHomeCover;
typedef struct {
  XpUiHomeCover covers[3];
  int32_t cover_width;
  uint8_t rendered, restored, can_snapshot, reserved;
} XpUiHomePrepared;
typedef struct {
  int32_t lyra, rounded;
} XpUiHomeWidths;
typedef struct {
  uint32_t first;
  uint8_t count, reserved;
  int16_t max_width;
} XpUiHomeLines;
typedef struct {
  void* context;
  int32_t (*width)(void*, uint32_t, int32_t, uint8_t);
  int32_t (*line)(void*, int32_t);
  uint8_t (*empty)(void*, uint32_t);
  XpUiHomeLines (*wrap)(void*, uint32_t, int32_t, int16_t, uint8_t, uint8_t);
  uint32_t (*truncate)(void*, uint32_t, int32_t, int16_t, uint8_t);
} XpUiHomeTexts;

enum {
  XP_UI_HOME_FILES = 100,
  XP_UI_HOME_LIBRARY,
  XP_UI_HOME_OPDS,
  XP_UI_HOME_PLUGINS,
  XP_UI_HOME_TRANSFER,
  XP_UI_HOME_SETTINGS
};
enum { XP_UI_HOME_VIEW_INVALID, XP_UI_HOME_VIEW_OK, XP_UI_HOME_VIEW_CAPACITY };
#define XP_UI_HOME_HEADER 0x80000001u
#define XP_UI_HOME_FOOTER 0x80000002u
#define XP_UI_HOME_RESTORE 0x80000003u
#define XP_UI_HOME_SNAPSHOT 0x80000004u
#define XP_UI_HOME_MASK_CORNERS 0x80000005u
#define XP_UI_HOME_RIBBON 0x80000006u
#define XP_UI_HOME_PLACEHOLDER 0x80000007u
#define XP_UI_HOME_COVER_ICON 0x80000008u
#define XP_UI_HOME_NO_OPEN_BOOK 0x80000010u
#define XP_UI_HOME_START_READING 0x80000011u
#define XP_UI_HOME_CONTINUE 0x80000012u
#define XP_UI_HOME_COVER 0x1000u
#define XP_UI_HOME_TITLE 0x2000u
#define XP_UI_HOME_AUTHOR 0x3000u
#define XP_UI_HOME_MENU 0x4000u
#define XP_UI_HOME_MENU_ICON 0x5000u

void xp_ui_home_init(XpUiHomeState*, uint8_t books, uint8_t grid, uint8_t opds, uint8_t plugins, uint8_t continuing,
                     int32_t initial);
XpUiInterest xp_ui_home_interest(const XpUiHomeState*, uint32_t now);
XpUiHomeUpdate xp_ui_home_update(XpUiHomeState*, const XpUiInput*, const XpUiHomeTouch*);
uint8_t xp_ui_home_menu_keys(const XpUiHomeState*, uint8_t continuing_in_menu, uint8_t* keys, uint8_t capacity);
XpUiEffect xp_ui_home_painted(XpUiHomeState*, uint8_t thumb_changed);
void xp_ui_home_loaded(XpUiHomeState*);
void xp_ui_home_prepare_widths(XpUiHomeWidths*, const XpUiHomeState*, const XpUiHomeLayout*, XpUiHomePrepared*);
int32_t xp_ui_home_thumb_height(const XpUiHomeLayout*);
// Outputs remain unchanged on failure. Scratch commands may change. No hit array.
uint8_t xp_ui_home_standard_view(const XpUiHomeState*, const XpUiHomeLayout*, const XpUiHomePrepared*,
                                 const XpUiHomeTexts*, const XpUiBuffer*, uint8_t clean_initial, XpUiHomeTouch*,
                                 XpUiScene*);

typedef struct {
  XpUiRect screen, safe;
  int32_t body_font, small_font, title_font, progress;
  int16_t space_sm, space_lg, header_side, header_top, header_height, corner;
  int16_t tab_height, tab_height_pct, tab_width_pct, tab_offset;
  uint8_t touch, body_bold, reserved[2];
} XpUiHomeGridLayout;
typedef struct {
  XpUiRect screen, hero, grid, tabs;
  int16_t cell_width, row_height, gap, tab_width, thumb_height, min_touch;
  uint8_t books, tab_count, grid_distributed, tab_distributed;
} XpUiHomeGridGeometry;
typedef struct {
  void* context;
  int32_t (*width)(void*, uint32_t, int32_t);
  int32_t (*line)(void*, int32_t);
  int32_t (*height)(void*, uint32_t, int32_t, int16_t, uint8_t);
  uint8_t (*empty)(void*, uint32_t);
} XpUiHomeGridTexts;
#define XP_UI_HOME_PROGRESS 0x80000013u
#define XP_UI_HOME_GRID_COVER 0x6000u
#define XP_UI_HOME_GRID_ICON 0x7000u
uint8_t xp_ui_home_grid_view(const XpUiHomeState*, const XpUiHomeGridLayout*, const XpUiHomeGridTexts*,
                             const XpUiBuffer*, uint8_t clean_initial, XpUiHomeGridGeometry*, XpUiScene*);
XpUiHomeUpdate xp_ui_home_grid_update(XpUiHomeState*, const XpUiInput*, const XpUiHomeGridGeometry*);

typedef struct {
  uint32_t label, subtitle, value, icon, heading;
  int16_t icon_size;
  uint16_t reserved;
} XpUiHomeRow;
typedef struct {
  void* context;
  uint8_t (*window)(void*, uint16_t, uint16_t);
  uint8_t (*row)(void*, uint16_t, XpUiHomeRow*);
  int32_t (*width)(void*, uint32_t, int32_t, uint8_t);
  int32_t (*height)(void*, uint32_t, int32_t, uint8_t, uint8_t, int16_t);
  int32_t (*line)(void*, int32_t);
  const uint8_t* path;
  uint32_t path_len;
  uint8_t (*position)(void*, int32_t, uint16_t, uint8_t);
} XpUiHomeRows;
typedef struct {
  XpUiRect first;
  int32_t first_index;
  int16_t heights[24], offsets[24];
  XpUiReaderControl controls[7];
  int16_t stride;
  uint8_t count, control_count, uniform, reserved[3];
} XpUiHomeListHits;
typedef struct {
  XpUiListState list;
  int32_t revealed;
  uint8_t picker, reserved[3];
} XpUiFileState;
typedef struct {
  uint16_t count;
  uint8_t root, directory, long_confirm, long_touch, touch, touch_directory;
} XpUiFileFacts;
typedef struct {
  XpUiEffect effect;
  uint8_t consumed, haptic, reserved[2];
} XpUiFileUpdate;
typedef struct {
  XpUiReaderTheme theme;
  int16_t hints_height, side_padding;
} XpUiFileLayout;
typedef struct {
  XpUiListState list;
  uint32_t path_start;
  uint8_t path_truncated, back_home, select_file, has_rows;
} XpUiFileFeedback;
enum {
  XP_UI_FILE_ROOT = 4,
  XP_UI_FILE_PARENT,
  XP_UI_FILE_HOME,
  XP_UI_FILE_OPTIONS,
  XP_UI_FILE_DIRECTORY,
  XP_UI_FILE_PICK,
  XP_UI_FILE_OPEN,
  XP_UI_FILE_DELETE,
  XP_UI_FILE_RENAMED
};
#define XP_UI_FILE_HEADER_FOLDER 0x80000100u
#define XP_UI_FILE_HEADER_ROOT 0x80000101u
#define XP_UI_FILE_HEADER_PICKER 0x80000102u
#define XP_UI_FILE_FOOTER 0x80000103u
#define XP_UI_FILE_PATH 0x80000104u
#define XP_UI_FILE_EMPTY 0x80000105u
#define XP_UI_FILE_NO_BIN 0x80000106u
#define XP_UI_FILE_ELLIPSIS 0x80000107u
#define XP_UI_FILE_PATH_SUFFIX 0x10000000u
void xp_ui_file_init(XpUiFileState*, uint8_t picker);
XpUiInterest xp_ui_file_interest(const XpUiFileState*, uint32_t now);
XpUiFileUpdate xp_ui_file_update(XpUiFileState*, const XpUiInput*, const XpUiFileFacts*);
uint8_t xp_ui_file_view(const XpUiFileState*, const XpUiFileFacts*, const XpUiFileLayout*, const XpUiHomeRows*,
                        const XpUiBuffer*, XpUiFileFeedback*, XpUiHomeListHits*, XpUiScene*);
void xp_ui_file_publish(XpUiFileState*, const XpUiFileFeedback*, int32_t consumed_scroll);
void xp_ui_file_loaded(XpUiFileState*, uint8_t reason, int32_t selected, uint16_t count);
uint8_t xp_ui_home_list_hit(const XpUiHomeListHits*, int16_t x, int16_t y, XpUiInteraction*);

typedef struct {
  XpUiListState lists[3], expanded;
  int32_t revealed;
  uint32_t last_repeat;
  uint8_t active_tab, descending_tabs, sort, degraded, collapsed, queried;
  uint8_t suppress_confirm, suppress_back, started_on_tabs, reserved[3];
} XpUiLibraryState;
typedef struct {
  uint16_t count, books;
  uint8_t long_confirm, long_touch, touch, filter_failed;
} XpUiLibraryFacts;
typedef XpUiFileUpdate XpUiLibraryUpdate;
typedef struct {
  XpUiRect screen, safe;
  int32_t body_font, small_font;
  int16_t height, gap, side, divider, spacing;
  uint8_t full, touch, radius, bold, reserved[2];
} XpUiLibraryTabs;
typedef struct {
  XpUiReaderTheme theme;
  XpUiLibraryTabs tabs;
  XpUiRect content;
  int16_t hints_height, corner;
} XpUiLibraryLayout;
typedef struct {
  XpUiListState list;
  XpUiRect body;
  uint8_t back_home, confirm, search, window_capacity;
} XpUiLibraryFeedback;
enum {
  XP_UI_LIBRARY_HOME = 4,
  XP_UI_LIBRARY_SEARCH,
  XP_UI_LIBRARY_OPTIONS,
  XP_UI_LIBRARY_DELETE,
  XP_UI_LIBRARY_COLLAPSE,
  XP_UI_LIBRARY_EXPAND,
  XP_UI_LIBRARY_CLEAR_QUERY,
  XP_UI_LIBRARY_SORT,
  XP_UI_LIBRARY_REBUILD,
  XP_UI_LIBRARY_EXPAND_SELECTED
};
enum {
  XP_UI_LIBRARY_DEGRADED = 1,
  XP_UI_LIBRARY_SUPPRESS,
  XP_UI_LIBRARY_FILTER_STARTED,
  XP_UI_LIBRARY_QUERY_LOADED,
  XP_UI_LIBRARY_SORT_BEGIN,
  XP_UI_LIBRARY_SORT_FINISH,
  XP_UI_LIBRARY_REBUILT,
  XP_UI_LIBRARY_COLLAPSED,
  XP_UI_LIBRARY_EXPANDED,
  XP_UI_LIBRARY_QUERY_CLEARED,
  XP_UI_LIBRARY_SELECT,
  XP_UI_LIBRARY_CLOSE_REVEAL
};
#define XP_UI_LIBRARY_TAB 0x80000200u
#define XP_UI_LIBRARY_UP 0x80000203u
#define XP_UI_LIBRARY_DOWN 0x80000204u
#define XP_UI_LIBRARY_HEADER 0x80000210u
#define XP_UI_LIBRARY_FOOTER 0x80000211u
#define XP_UI_LIBRARY_POSITION 0x80000212u
#define XP_UI_LIBRARY_EMPTY 0x80000213u
#define XP_UI_LIBRARY_NO_RESULTS 0x80000214u
#define XP_UI_LIBRARY_UNAVAILABLE 0x80000215u
#define XP_UI_LIBRARY_HELP 0x80000220u
void xp_ui_library_init(XpUiLibraryState*);
void xp_ui_library_event(XpUiLibraryState*, uint8_t event, int32_t value, uint16_t count);
uint8_t xp_ui_library_flags(const XpUiLibraryState*, uint16_t books);
int32_t xp_ui_library_entry(const XpUiLibraryState*);
uint8_t xp_ui_library_pinned(const XpUiLibraryState*, uint8_t total);
uint16_t xp_ui_library_books(const XpUiLibraryState*, uint16_t indexed, uint16_t filtered, uint8_t pins,
                             uint8_t overlap);
int32_t xp_ui_library_row(const XpUiLibraryState*, int32_t entry, uint8_t pins, const uint16_t* filtered,
                          uint16_t filtered_count, const uint16_t* overlap, uint8_t overlap_count);
int32_t xp_ui_library_group(int32_t book, const uint16_t* starts, uint16_t count);
uint8_t xp_ui_library_overlap(const uint16_t* ascending, uint8_t count, uint16_t total, uint8_t descending,
                              uint16_t* out);
XpUiInterest xp_ui_library_interest(const XpUiLibraryState*, uint32_t now);
XpUiLibraryUpdate xp_ui_library_update(XpUiLibraryState*, const XpUiInput*, const XpUiLibraryFacts*);
uint8_t xp_ui_library_view(const XpUiLibraryState*, const XpUiLibraryFacts*, const XpUiLibraryLayout*,
                           const XpUiHomeRows*, const XpUiBuffer*, XpUiLibraryFeedback*, XpUiHomeListHits*, XpUiScene*);
void xp_ui_library_publish(XpUiLibraryState*, const XpUiLibraryFeedback*, uint8_t tab, int32_t scroll);
uint8_t xp_ui_home_list_header(XpUiHomeListHits*, const XpUiReaderControl*, uint8_t count);

typedef struct {
  XpUiRect rect;
  uint32_t offset, bytes;
  uint8_t valid, reserved[3];
} XpUiHomeCacheSlot;
typedef struct {
  XpUiHomeCacheSlot slots[7];
  uint32_t capacity, used;
  int32_t orientation;
} XpUiHomeCache;
typedef struct {
  uint32_t offset, bytes;
} XpUiHomeCacheSpan;
uint32_t xp_ui_home_cache_init(XpUiHomeCache*, uint32_t region_bytes, int32_t width, int32_t height);
void xp_ui_home_cache_allocated(XpUiHomeCache*, uint8_t success);
void xp_ui_home_cache_prepare(XpUiHomeCache*, int32_t orientation);
void xp_ui_home_cache_invalidate(XpUiHomeCache*);
void xp_ui_home_cache_invalidate_slot(XpUiHomeCache*, uint8_t index);
XpUiHomeCacheSpan xp_ui_home_cache_lookup(const XpUiHomeCache*, uint8_t index, XpUiRect);
XpUiHomeCacheSpan xp_ui_home_cache_reserve(XpUiHomeCache*, uint8_t index, XpUiRect, uint32_t needed);
void xp_ui_home_cache_copied(XpUiHomeCache*, uint8_t index, uint8_t success);

#ifdef __cplusplus
}
#include <stddef.h>
#define XP_HOME_SIZE(T, S, A) static_assert(sizeof(T) == (S) && alignof(T) == (A))
#define XP_HOME_OFFSET(T, F, O) static_assert(offsetof(T, F) == (O))
XP_HOME_SIZE(XpUiHomeState, 16, 4);
XP_HOME_OFFSET(XpUiHomeState, selected, 0);
XP_HOME_OFFSET(XpUiHomeState, last_repeat, 4);
XP_HOME_OFFSET(XpUiHomeState, books, 8);
XP_HOME_OFFSET(XpUiHomeState, grid, 9);
XP_HOME_OFFSET(XpUiHomeState, opds, 10);
XP_HOME_OFFSET(XpUiHomeState, plugins, 11);
XP_HOME_OFFSET(XpUiHomeState, continuing, 12);
XP_HOME_OFFSET(XpUiHomeState, first_render, 13);
XP_HOME_OFFSET(XpUiHomeState, covers_loaded, 14);
XP_HOME_OFFSET(XpUiHomeState, loading, 15);
XP_HOME_SIZE(XpUiHomeTouch, 20, 2);
XP_HOME_OFFSET(XpUiHomeTouch, cover, 0);
XP_HOME_OFFSET(XpUiHomeTouch, menu, 8);
XP_HOME_OFFSET(XpUiHomeTouch, menu_step, 16);
XP_HOME_OFFSET(XpUiHomeTouch, columns, 18);
XP_HOME_OFFSET(XpUiHomeTouch, continue_in_menu, 19);
XP_HOME_SIZE(XpUiHomeUpdate, 12, 4);
XP_HOME_OFFSET(XpUiHomeUpdate, effect, 0);
XP_HOME_OFFSET(XpUiHomeUpdate, haptic, 8);
XP_HOME_OFFSET(XpUiHomeUpdate, reserved, 9);
XP_HOME_SIZE(XpUiHomeLayout, 68, 4);
XP_HOME_OFFSET(XpUiHomeLayout, screen, 0);
XP_HOME_OFFSET(XpUiHomeLayout, content, 8);
XP_HOME_OFFSET(XpUiHomeLayout, safe, 16);
XP_HOME_OFFSET(XpUiHomeLayout, title_font, 24);
XP_HOME_OFFSET(XpUiHomeLayout, small_font, 28);
XP_HOME_OFFSET(XpUiHomeLayout, tiny_font, 32);
XP_HOME_OFFSET(XpUiHomeLayout, top_padding, 36);
XP_HOME_OFFSET(XpUiHomeLayout, header_height, 38);
XP_HOME_OFFSET(XpUiHomeLayout, home_top, 40);
XP_HOME_OFFSET(XpUiHomeLayout, tile_height, 42);
XP_HOME_OFFSET(XpUiHomeLayout, cover_height, 44);
XP_HOME_OFFSET(XpUiHomeLayout, side_padding, 46);
XP_HOME_OFFSET(XpUiHomeLayout, spacing, 48);
XP_HOME_OFFSET(XpUiHomeLayout, menu_top, 50);
XP_HOME_OFFSET(XpUiHomeLayout, menu_height, 52);
XP_HOME_OFFSET(XpUiHomeLayout, menu_gap, 54);
XP_HOME_OFFSET(XpUiHomeLayout, hints_height, 56);
XP_HOME_OFFSET(XpUiHomeLayout, scroll_width, 58);
XP_HOME_OFFSET(XpUiHomeLayout, scroll_offset, 60);
XP_HOME_OFFSET(XpUiHomeLayout, theme, 62);
XP_HOME_OFFSET(XpUiHomeLayout, continue_in_menu, 63);
XP_HOME_OFFSET(XpUiHomeLayout, columns, 64);
XP_HOME_OFFSET(XpUiHomeLayout, reserved, 65);
XP_HOME_SIZE(XpUiHomeCover, 12, 4);
XP_HOME_OFFSET(XpUiHomeCover, width, 0);
XP_HOME_OFFSET(XpUiHomeCover, height, 4);
XP_HOME_OFFSET(XpUiHomeCover, status, 8);
XP_HOME_OFFSET(XpUiHomeCover, reserved, 9);
XP_HOME_SIZE(XpUiHomePrepared, 44, 4);
XP_HOME_OFFSET(XpUiHomePrepared, covers, 0);
XP_HOME_OFFSET(XpUiHomePrepared, cover_width, 36);
XP_HOME_OFFSET(XpUiHomePrepared, rendered, 40);
XP_HOME_OFFSET(XpUiHomePrepared, restored, 41);
XP_HOME_OFFSET(XpUiHomePrepared, can_snapshot, 42);
XP_HOME_OFFSET(XpUiHomePrepared, reserved, 43);
XP_HOME_SIZE(XpUiHomeWidths, 8, 4);
XP_HOME_OFFSET(XpUiHomeWidths, lyra, 0);
XP_HOME_OFFSET(XpUiHomeWidths, rounded, 4);
XP_HOME_SIZE(XpUiHomeLines, 8, 4);
XP_HOME_OFFSET(XpUiHomeLines, first, 0);
XP_HOME_OFFSET(XpUiHomeLines, count, 4);
XP_HOME_OFFSET(XpUiHomeLines, reserved, 5);
XP_HOME_OFFSET(XpUiHomeLines, max_width, 6);
XP_HOME_SIZE(XpUiHomeTexts, sizeof(void*) * 6, sizeof(void*));
XP_HOME_OFFSET(XpUiHomeTexts, context, sizeof(void*) * 0);
XP_HOME_OFFSET(XpUiHomeTexts, width, sizeof(void*) * 1);
XP_HOME_OFFSET(XpUiHomeTexts, line, sizeof(void*) * 2);
XP_HOME_OFFSET(XpUiHomeTexts, empty, sizeof(void*) * 3);
XP_HOME_OFFSET(XpUiHomeTexts, wrap, sizeof(void*) * 4);
XP_HOME_OFFSET(XpUiHomeTexts, truncate, sizeof(void*) * 5);
XP_HOME_SIZE(XpUiHomeGridLayout, 56, 4);
XP_HOME_OFFSET(XpUiHomeGridLayout, screen, 0);
XP_HOME_OFFSET(XpUiHomeGridLayout, safe, 8);
XP_HOME_OFFSET(XpUiHomeGridLayout, body_font, 16);
XP_HOME_OFFSET(XpUiHomeGridLayout, small_font, 20);
XP_HOME_OFFSET(XpUiHomeGridLayout, title_font, 24);
XP_HOME_OFFSET(XpUiHomeGridLayout, progress, 28);
XP_HOME_OFFSET(XpUiHomeGridLayout, space_sm, 32);
XP_HOME_OFFSET(XpUiHomeGridLayout, space_lg, 34);
XP_HOME_OFFSET(XpUiHomeGridLayout, header_side, 36);
XP_HOME_OFFSET(XpUiHomeGridLayout, header_top, 38);
XP_HOME_OFFSET(XpUiHomeGridLayout, header_height, 40);
XP_HOME_OFFSET(XpUiHomeGridLayout, corner, 42);
XP_HOME_OFFSET(XpUiHomeGridLayout, tab_height, 44);
XP_HOME_OFFSET(XpUiHomeGridLayout, tab_height_pct, 46);
XP_HOME_OFFSET(XpUiHomeGridLayout, tab_width_pct, 48);
XP_HOME_OFFSET(XpUiHomeGridLayout, tab_offset, 50);
XP_HOME_OFFSET(XpUiHomeGridLayout, touch, 52);
XP_HOME_OFFSET(XpUiHomeGridLayout, body_bold, 53);
XP_HOME_OFFSET(XpUiHomeGridLayout, reserved, 54);
XP_HOME_SIZE(XpUiHomeGridGeometry, 48, 2);
XP_HOME_OFFSET(XpUiHomeGridGeometry, screen, 0);
XP_HOME_OFFSET(XpUiHomeGridGeometry, hero, 8);
XP_HOME_OFFSET(XpUiHomeGridGeometry, grid, 16);
XP_HOME_OFFSET(XpUiHomeGridGeometry, tabs, 24);
XP_HOME_OFFSET(XpUiHomeGridGeometry, cell_width, 32);
XP_HOME_OFFSET(XpUiHomeGridGeometry, row_height, 34);
XP_HOME_OFFSET(XpUiHomeGridGeometry, gap, 36);
XP_HOME_OFFSET(XpUiHomeGridGeometry, tab_width, 38);
XP_HOME_OFFSET(XpUiHomeGridGeometry, thumb_height, 40);
XP_HOME_OFFSET(XpUiHomeGridGeometry, min_touch, 42);
XP_HOME_OFFSET(XpUiHomeGridGeometry, books, 44);
XP_HOME_OFFSET(XpUiHomeGridGeometry, tab_count, 45);
XP_HOME_OFFSET(XpUiHomeGridGeometry, grid_distributed, 46);
XP_HOME_OFFSET(XpUiHomeGridGeometry, tab_distributed, 47);
XP_HOME_SIZE(XpUiHomeGridTexts, sizeof(void*) * 5, sizeof(void*));
XP_HOME_OFFSET(XpUiHomeGridTexts, context, sizeof(void*) * 0);
XP_HOME_OFFSET(XpUiHomeGridTexts, width, sizeof(void*) * 1);
XP_HOME_OFFSET(XpUiHomeGridTexts, line, sizeof(void*) * 2);
XP_HOME_OFFSET(XpUiHomeGridTexts, height, sizeof(void*) * 3);
XP_HOME_OFFSET(XpUiHomeGridTexts, empty, sizeof(void*) * 4);
XP_HOME_SIZE(XpUiHomeRow, 24, 4);
XP_HOME_OFFSET(XpUiHomeRow, label, 0);
XP_HOME_OFFSET(XpUiHomeRow, subtitle, 4);
XP_HOME_OFFSET(XpUiHomeRow, value, 8);
XP_HOME_OFFSET(XpUiHomeRow, icon, 12);
XP_HOME_OFFSET(XpUiHomeRow, heading, 16);
XP_HOME_OFFSET(XpUiHomeRow, icon_size, 20);
XP_HOME_OFFSET(XpUiHomeRow, reserved, 22);
XP_HOME_SIZE(XpUiHomeRows, sizeof(void*) * 9, sizeof(void*));
XP_HOME_OFFSET(XpUiHomeRows, context, sizeof(void*) * 0);
XP_HOME_OFFSET(XpUiHomeRows, window, sizeof(void*) * 1);
XP_HOME_OFFSET(XpUiHomeRows, row, sizeof(void*) * 2);
XP_HOME_OFFSET(XpUiHomeRows, width, sizeof(void*) * 3);
XP_HOME_OFFSET(XpUiHomeRows, height, sizeof(void*) * 4);
XP_HOME_OFFSET(XpUiHomeRows, line, sizeof(void*) * 5);
XP_HOME_OFFSET(XpUiHomeRows, path, sizeof(void*) * 6);
XP_HOME_OFFSET(XpUiHomeRows, path_len, sizeof(void*) * 7);
XP_HOME_OFFSET(XpUiHomeRows, position, sizeof(void*) * 8);
XP_HOME_SIZE(XpUiHomeListHits, 228, 4);
XP_HOME_OFFSET(XpUiHomeListHits, first, 0);
XP_HOME_OFFSET(XpUiHomeListHits, first_index, 8);
XP_HOME_OFFSET(XpUiHomeListHits, heights, 12);
XP_HOME_OFFSET(XpUiHomeListHits, offsets, 60);
XP_HOME_OFFSET(XpUiHomeListHits, controls, 108);
XP_HOME_OFFSET(XpUiHomeListHits, stride, 220);
XP_HOME_OFFSET(XpUiHomeListHits, count, 222);
XP_HOME_OFFSET(XpUiHomeListHits, control_count, 223);
XP_HOME_OFFSET(XpUiHomeListHits, uniform, 224);
XP_HOME_OFFSET(XpUiHomeListHits, reserved, 225);
XP_HOME_SIZE(XpUiFileState, 48, 4);
XP_HOME_OFFSET(XpUiFileState, list, 0);
XP_HOME_OFFSET(XpUiFileState, revealed, 40);
XP_HOME_OFFSET(XpUiFileState, picker, 44);
XP_HOME_OFFSET(XpUiFileState, reserved, 45);
XP_HOME_SIZE(XpUiFileFacts, 8, 2);
XP_HOME_OFFSET(XpUiFileFacts, count, 0);
XP_HOME_OFFSET(XpUiFileFacts, root, 2);
XP_HOME_OFFSET(XpUiFileFacts, directory, 3);
XP_HOME_OFFSET(XpUiFileFacts, long_confirm, 4);
XP_HOME_OFFSET(XpUiFileFacts, long_touch, 5);
XP_HOME_OFFSET(XpUiFileFacts, touch, 6);
XP_HOME_OFFSET(XpUiFileFacts, touch_directory, 7);
XP_HOME_SIZE(XpUiFileUpdate, 12, 4);
XP_HOME_OFFSET(XpUiFileUpdate, effect, 0);
XP_HOME_OFFSET(XpUiFileUpdate, consumed, 8);
XP_HOME_OFFSET(XpUiFileUpdate, haptic, 9);
XP_HOME_OFFSET(XpUiFileUpdate, reserved, 10);
XP_HOME_SIZE(XpUiFileLayout, 76, 4);
XP_HOME_OFFSET(XpUiFileLayout, theme, 0);
XP_HOME_OFFSET(XpUiFileLayout, hints_height, 72);
XP_HOME_OFFSET(XpUiFileLayout, side_padding, 74);
XP_HOME_SIZE(XpUiFileFeedback, 48, 4);
XP_HOME_OFFSET(XpUiFileFeedback, list, 0);
XP_HOME_OFFSET(XpUiFileFeedback, path_start, 40);
XP_HOME_OFFSET(XpUiFileFeedback, path_truncated, 44);
XP_HOME_OFFSET(XpUiFileFeedback, back_home, 45);
XP_HOME_OFFSET(XpUiFileFeedback, select_file, 46);
XP_HOME_OFFSET(XpUiFileFeedback, has_rows, 47);
XP_HOME_SIZE(XpUiLibraryState, 180, 4);
XP_HOME_OFFSET(XpUiLibraryState, lists, 0);
XP_HOME_OFFSET(XpUiLibraryState, expanded, 120);
XP_HOME_OFFSET(XpUiLibraryState, revealed, 160);
XP_HOME_OFFSET(XpUiLibraryState, last_repeat, 164);
XP_HOME_OFFSET(XpUiLibraryState, active_tab, 168);
XP_HOME_OFFSET(XpUiLibraryState, descending_tabs, 169);
XP_HOME_OFFSET(XpUiLibraryState, sort, 170);
XP_HOME_OFFSET(XpUiLibraryState, degraded, 171);
XP_HOME_OFFSET(XpUiLibraryState, collapsed, 172);
XP_HOME_OFFSET(XpUiLibraryState, queried, 173);
XP_HOME_OFFSET(XpUiLibraryState, suppress_confirm, 174);
XP_HOME_OFFSET(XpUiLibraryState, suppress_back, 175);
XP_HOME_OFFSET(XpUiLibraryState, started_on_tabs, 176);
XP_HOME_OFFSET(XpUiLibraryState, reserved, 177);
XP_HOME_SIZE(XpUiLibraryFacts, 8, 2);
XP_HOME_OFFSET(XpUiLibraryFacts, count, 0);
XP_HOME_OFFSET(XpUiLibraryFacts, books, 2);
XP_HOME_OFFSET(XpUiLibraryFacts, long_confirm, 4);
XP_HOME_OFFSET(XpUiLibraryFacts, long_touch, 5);
XP_HOME_OFFSET(XpUiLibraryFacts, touch, 6);
XP_HOME_OFFSET(XpUiLibraryFacts, filter_failed, 7);
XP_HOME_SIZE(XpUiLibraryUpdate, 12, 4);
XP_HOME_OFFSET(XpUiLibraryUpdate, effect, 0);
XP_HOME_OFFSET(XpUiLibraryUpdate, consumed, 8);
XP_HOME_OFFSET(XpUiLibraryUpdate, haptic, 9);
XP_HOME_OFFSET(XpUiLibraryUpdate, reserved, 10);
XP_HOME_SIZE(XpUiLibraryTabs, 40, 4);
XP_HOME_OFFSET(XpUiLibraryTabs, screen, 0);
XP_HOME_OFFSET(XpUiLibraryTabs, safe, 8);
XP_HOME_OFFSET(XpUiLibraryTabs, body_font, 16);
XP_HOME_OFFSET(XpUiLibraryTabs, small_font, 20);
XP_HOME_OFFSET(XpUiLibraryTabs, height, 24);
XP_HOME_OFFSET(XpUiLibraryTabs, gap, 26);
XP_HOME_OFFSET(XpUiLibraryTabs, side, 28);
XP_HOME_OFFSET(XpUiLibraryTabs, divider, 30);
XP_HOME_OFFSET(XpUiLibraryTabs, spacing, 32);
XP_HOME_OFFSET(XpUiLibraryTabs, full, 34);
XP_HOME_OFFSET(XpUiLibraryTabs, touch, 35);
XP_HOME_OFFSET(XpUiLibraryTabs, radius, 36);
XP_HOME_OFFSET(XpUiLibraryTabs, bold, 37);
XP_HOME_OFFSET(XpUiLibraryTabs, reserved, 38);
XP_HOME_SIZE(XpUiLibraryLayout, 124, 4);
XP_HOME_OFFSET(XpUiLibraryLayout, theme, 0);
XP_HOME_OFFSET(XpUiLibraryLayout, tabs, 72);
XP_HOME_OFFSET(XpUiLibraryLayout, content, 112);
XP_HOME_OFFSET(XpUiLibraryLayout, hints_height, 120);
XP_HOME_OFFSET(XpUiLibraryLayout, corner, 122);
XP_HOME_SIZE(XpUiLibraryFeedback, 52, 4);
XP_HOME_OFFSET(XpUiLibraryFeedback, list, 0);
XP_HOME_OFFSET(XpUiLibraryFeedback, body, 40);
XP_HOME_OFFSET(XpUiLibraryFeedback, back_home, 48);
XP_HOME_OFFSET(XpUiLibraryFeedback, confirm, 49);
XP_HOME_OFFSET(XpUiLibraryFeedback, search, 50);
XP_HOME_OFFSET(XpUiLibraryFeedback, window_capacity, 51);
XP_HOME_SIZE(XpUiHomeCacheSlot, 20, 4);
XP_HOME_OFFSET(XpUiHomeCacheSlot, rect, 0);
XP_HOME_OFFSET(XpUiHomeCacheSlot, offset, 8);
XP_HOME_OFFSET(XpUiHomeCacheSlot, bytes, 12);
XP_HOME_OFFSET(XpUiHomeCacheSlot, valid, 16);
XP_HOME_OFFSET(XpUiHomeCacheSlot, reserved, 17);
XP_HOME_SIZE(XpUiHomeCache, 152, 4);
XP_HOME_OFFSET(XpUiHomeCache, slots, 0);
XP_HOME_OFFSET(XpUiHomeCache, capacity, 140);
XP_HOME_OFFSET(XpUiHomeCache, used, 144);
XP_HOME_OFFSET(XpUiHomeCache, orientation, 148);
XP_HOME_SIZE(XpUiHomeCacheSpan, 8, 4);
XP_HOME_OFFSET(XpUiHomeCacheSpan, offset, 0);
XP_HOME_OFFSET(XpUiHomeCacheSpan, bytes, 4);
#undef XP_HOME_SIZE
#undef XP_HOME_OFFSET
#endif
