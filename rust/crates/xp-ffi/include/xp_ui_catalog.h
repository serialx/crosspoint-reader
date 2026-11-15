#pragma once
#include "xp_ui.h"
#ifdef __cplusplus
extern "C" {
#endif

// Private catalog-family ABI. Descriptors are aligned, disjoint, and live for
// each call. Callbacks borrow strings through painting; no reentry or retention.
typedef enum {
  XP_UI_CATALOG_SERVERS,
  XP_UI_CATALOG_EDITOR,
  XP_UI_CATALOG_BROWSER,
  XP_UI_CATALOG_PLUGINS,
  XP_UI_CATALOG_HUB
} XpUiCatalogKind;
typedef enum {
  XP_UI_CATALOG_CHECK_WIFI,
  XP_UI_CATALOG_WIFI_SELECTION,
  XP_UI_CATALOG_SEARCH_INPUT,
  XP_UI_CATALOG_LOADING,
  XP_UI_CATALOG_BROWSING,
  XP_UI_CATALOG_DOWNLOADING,
  XP_UI_CATALOG_ERROR,
  XP_UI_CATALOG_PLUGIN_PICKER,
  XP_UI_CATALOG_LIST_PICKER,
  XP_UI_CATALOG_DONE,
  XP_UI_CATALOG_NO_TOKEN,
  XP_UI_CATALOG_AUTH = 11
} XpUiCatalogMode;
typedef struct {
  XpUiListState list;
  uint32_t progress, total, last_progress_ms;
  int32_t last_percent, server_index;
  uint8_t kind, mode, search_pending, cancel;
  uint8_t picker, new_server, save_error, reserved;
  int32_t page, current_list;
  uint32_t auth_next_poll_ms;
  uint8_t plugin_flags, popup, ignore_back, long_press;
} XpUiCatalogState;
typedef struct {
  const char *label, *subtitle, *value, *heading;
} XpUiCatalogRow;
typedef struct {
  void* context;
  uint8_t (*row)(void*, uint16_t, XpUiCatalogRow*);
  const char* (*text)(void*, uint32_t);
  int32_t (*width)(void*, const char*, int32_t, uint8_t);
  int32_t (*height)(void*, const char*, int32_t, uint8_t, uint8_t, int16_t);
  int32_t (*line)(void*, int32_t);
  uint16_t count, reserved;
} XpUiCatalogResources;
typedef struct {
  XpUiListStyle list;
  XpUiRect safe, search_button;
  int32_t small_font, title_font;
  int16_t top_padding, header_height, hints_height, spacing, tab_height, side_padding;
  int16_t space_md, button_height;
  int32_t page_current, page_total;
  uint8_t touch, search, pages, control_radius;
  int16_t popup_padding, popup_gap, popup_side_margin, selection_padding;
  uint8_t popup_radius, popup_border;
  uint16_t reserved;
} XpUiCatalogLayout;
typedef struct {
  XpUiRect row_area;
  int32_t first_index;
  int16_t offsets[32], heights[32];
  XpUiInteraction controls[6];
  uint8_t rows, control_count;
  uint16_t reserved;
} XpUiCatalogHits;
enum { XP_UI_CATALOG_SEARCH = 4, XP_UI_CATALOG_RETRY = 5, XP_UI_CATALOG_PAGE = 6, XP_UI_CATALOG_CANCEL_TRANSFER = 7 };
enum {
  XP_UI_CATALOG_COMPLETE = 8,
  XP_UI_CATALOG_REDRAW = 9,
  XP_UI_CATALOG_POLL_AUTH = 10,
  XP_UI_CATALOG_UNINSTALL = 11,
  XP_UI_CATALOG_HUB_OPTION = 12
};
enum { XP_UI_CATALOG_XML = 1, XP_UI_CATALOG_MORE = 2, XP_UI_CATALOG_DEVICE_CODE = 4 };
// Plugin paging state is caller-owned; callbacks do not run during these calls.
typedef struct {
  size_t start, limit, seen;
  uint8_t more;
} XpUiCatalogWindow;
uint8_t xp_ui_catalog_window_take(XpUiCatalogWindow*, uint8_t eligible);
uint8_t xp_ui_catalog_page_template(const char*, size_t);
// Returns -1 for Previous, item_count for Next, or the item index; mutates page
// only for pager rows. The caller then performs the existing fetch operation.
int32_t xp_ui_catalog_item(XpUiCatalogState*, int32_t row, int32_t item_count);
void xp_ui_catalog_init(XpUiCatalogState*, uint8_t kind, int32_t server_index, uint8_t picker);
void xp_ui_catalog_select(XpUiCatalogState*, int32_t index, uint8_t reset);
void xp_ui_catalog_mode(XpUiCatalogState*, uint8_t mode);
uint8_t xp_ui_catalog_awake(const XpUiCatalogState*);
uint8_t xp_ui_catalog_progress(XpUiCatalogState*, uint32_t downloaded, uint32_t total, uint32_t now_ms);
XpUiInterest xp_ui_catalog_interest(const XpUiCatalogState*, uint32_t now_ms);
XpUiEffect xp_ui_catalog_update(XpUiCatalogState*, const XpUiInput*, uint16_t count, uint8_t search, uint8_t pages);
void xp_ui_catalog_publish(XpUiCatalogState*, const XpUiListState*, int32_t consumed_scroll);
// Returns 1 on success, 2 for command capacity, or 0 for invalid input.
// Failure leaves next/list, scene, and hit outputs unchanged; commands are scratch.
uint8_t xp_ui_catalog_view(const XpUiCatalogState*, const XpUiCatalogLayout*, const XpUiCatalogResources*,
                           const XpUiBuffer*, XpUiListState*, XpUiScene*, XpUiCatalogHits*);
uint8_t xp_ui_catalog_hit(const XpUiCatalogHits*, int16_t x, int16_t y, XpUiInteraction*);
#ifdef __cplusplus
}
#endif
