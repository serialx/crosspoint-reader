#pragma once
#include "xp_ui.h"
#include "xp_ui_reader_chrome.h"

#ifdef __cplusplus
extern "C" {
#endif

// Private Home/Library/File Browser family. Strings are borrowed through paint;
// scene construction and resource callbacks run under the existing render lock.
enum { XP_UI_HOME_VIEW_INVALID, XP_UI_HOME_VIEW_OK, XP_UI_HOME_VIEW_CAPACITY };
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
XpUiEffect xp_ui_file_activate(XpUiFileState* state, const XpUiFileFacts* facts);
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

#ifdef __cplusplus
}
#include <stddef.h>
#define XP_HOME_SIZE(T, S, A) static_assert(sizeof(T) == (S) && alignof(T) == (A))
#define XP_HOME_OFFSET(T, F, O) static_assert(offsetof(T, F) == (O))
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
#undef XP_HOME_SIZE
#undef XP_HOME_OFFSET
#endif
