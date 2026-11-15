#pragma once
#include <xp_epub_session.h>

#ifdef __cplusplus
#include <type_traits>
#define CONTRACT(condition) static_assert(condition)
#define SIGNATURE(field, type) CONTRACT((std::is_same_v<decltype(XpEpubLeaves::field), type>))
#else
#define CONTRACT(condition) _Static_assert(condition, #condition)
#define SIGNATURE(field, type) CONTRACT(_Generic(((XpEpubLeaves*)0)->field, type: 1, default: 0))
#endif

#ifdef XP_SD_FONT_CATALOG
#define CATALOG_LEAF_INDEX(index) ((index) - ((index) >= 29 ? 1 : 0))
#define CATALOG_NATIVE_LEAF_INDEX(index) (CATALOG_LEAF_INDEX(index) - ((index) > 112 ? 1 : 0) - ((index) > 114 ? 1 : 0))
#else
#define CATALOG_LEAF_INDEX(index) (index)
#define CATALOG_NATIVE_LEAF_INDEX(index) (index)
#endif

#ifdef XP_APP_TEST
#define FONT_LEAF_INDEX(index)                                                                              \
  (CATALOG_LEAF_INDEX(index) - ((index) >= 9 ? 3 : 0) - ((index) >= 23 ? 2 : 0) - ((index) > 120 ? 2 : 0) - \
   ((index) > 123 ? 1 : 0))
#else
#define FONT_LEAF_INDEX(index)                                                                                     \
  (CATALOG_NATIVE_LEAF_INDEX(index) - ((index) >= 64 ? 6 : 0) - ((index) >= 97 ? 1 : 0) - ((index) >= 9 ? 3 : 0) - \
   ((index) >= 23 ? 2 : 0) - ((index) >= 124 ? 1 : 0) - ((index) >= 28 ? 4 : 0) - ((index) >= 137 ? 1 : 0) -       \
   ((index) >= 138 ? 1 : 0) - ((index) > 102 ? 1 : 0) - ((index) > 113 ? 1 : 0) - ((index) > 115 ? 1 : 0) -        \
   ((index) > 124 ? 1 : 0) - ((index) > 120 ? 2 : 0) - ((index) > 100 ? 1 : 0))
#endif

#ifndef XP_APP_TEST
typedef XpPluginSequence* (*EventCodec)(XpPluginEventStorage*, XpPluginEmitOps*);
typedef void (*EventAppend)(XpPluginAppendStorage*, void*, const XpPluginSubscriber*, XpPluginOps*);
SIGNATURE(event_codec, EventCodec);
SIGNATURE(event_append, EventAppend);
CONTRACT(offsetof(XpEpubLeaves, event_codec) == FONT_LEAF_INDEX(98) * sizeof(void (*)(void)));
CONTRACT(offsetof(XpEpubLeaves, event_append) == FONT_LEAF_INDEX(99) * sizeof(void (*)(void)));
#endif

typedef void (*ChildTheme)(XpUiReaderTheme*, XpUiReaderThemeFacts*, uint8_t);
typedef uint8_t (*ChildLayout)(XpUiUtilityLayout*);
typedef void (*ChildHeader)(XpUiRect, XpAppText, uint8_t);
typedef void (*ChildFooter)(XpUiRect, const XpAppText*);
typedef void (*ChildQr)(XpUiRect, XpAppText);
typedef void (*ChildIcon)(uint32_t, XpUiRect, XpUiRect);
typedef void (*ChildFonts)(void);
typedef void (*ChildPrewarm)(int32_t, const void*, XpEpubChildRowText, uint32_t);

CONTRACT(offsetof(XpEpubLeaves, clipping_physical) == FONT_LEAF_INDEX(162) * sizeof(void (*)(void)));
#ifdef XP_APP_TEST
CONTRACT(sizeof(XpEpubLeaves) == FONT_LEAF_INDEX(168) * sizeof(void (*)(void)));
#else
CONTRACT(sizeof(XpEpubLeaves) == FONT_LEAF_INDEX(170) * sizeof(void (*)(void)));
CONTRACT(offsetof(XpEpubLeaves, protection_finish) == FONT_LEAF_INDEX(168) * sizeof(void (*)(void)));
CONTRACT(offsetof(XpEpubLeaves, key_platform) == FONT_LEAF_INDEX(169) * sizeof(void (*)(void)));
#endif
CONTRACT(offsetof(XpEpubLeaves, child_list_theme) == FONT_LEAF_INDEX(130) * sizeof(void (*)(void)));
CONTRACT(offsetof(XpEpubLeaves, child_utility_layout) == FONT_LEAF_INDEX(131) * sizeof(void (*)(void)));
CONTRACT(offsetof(XpEpubLeaves, child_header) == FONT_LEAF_INDEX(132) * sizeof(void (*)(void)));
CONTRACT(offsetof(XpEpubLeaves, child_utility_footer) == FONT_LEAF_INDEX(133) * sizeof(void (*)(void)));
CONTRACT(offsetof(XpEpubLeaves, child_qr_draw) == FONT_LEAF_INDEX(134) * sizeof(void (*)(void)));
CONTRACT(offsetof(XpEpubLeaves, child_toolbar_icon) == FONT_LEAF_INDEX(135) * sizeof(void (*)(void)));
#ifdef XP_APP_TEST
CONTRACT(offsetof(XpEpubLeaves, child_clear_fonts) == FONT_LEAF_INDEX(136) * sizeof(void (*)(void)));
CONTRACT(offsetof(XpEpubLeaves, child_prewarm_rows) == FONT_LEAF_INDEX(137) * sizeof(void (*)(void)));
#endif
SIGNATURE(child_list_theme, ChildTheme);
SIGNATURE(child_utility_layout, ChildLayout);
SIGNATURE(child_header, ChildHeader);
SIGNATURE(child_utility_footer, ChildFooter);
SIGNATURE(child_qr_draw, ChildQr);
SIGNATURE(child_toolbar_icon, ChildIcon);
#ifdef XP_APP_TEST
SIGNATURE(child_clear_fonts, ChildFonts);
SIGNATURE(child_prewarm_rows, ChildPrewarm);
#endif
SIGNATURE(clipping_physical, const XpClippingPhysical*);

CONTRACT(offsetof(XpEpubLeaves, child_list_help) == FONT_LEAF_INDEX(163) * sizeof(void (*)(void)));
CONTRACT(offsetof(XpEpubLeaves, child_list_icon) == FONT_LEAF_INDEX(164) * sizeof(void (*)(void)));
typedef void (*ChildListHelp)(XpUiRect, XpAppText);
typedef void (*ChildListBookmark)(uint8_t, XpUiRect, XpUiRect, uint8_t);
SIGNATURE(child_list_help, ChildListHelp);
SIGNATURE(child_list_icon, ChildListBookmark);

#undef FONT_LEAF_INDEX
#undef CATALOG_LEAF_INDEX
#undef CATALOG_NATIVE_LEAF_INDEX
#undef SIGNATURE
#undef CONTRACT
