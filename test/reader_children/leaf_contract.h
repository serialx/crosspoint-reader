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

typedef void (*ChildTheme)(XpUiReaderTheme*, XpUiReaderThemeFacts*, uint8_t);
typedef uint8_t (*ChildLayout)(XpUiUtilityLayout*);
typedef void (*ChildHeader)(XpUiRect, XpAppText, uint8_t);
typedef void (*ChildFooter)(XpUiRect, const XpAppText*);
typedef void (*ChildQr)(XpUiRect, XpAppText);
typedef void (*ChildIcon)(uint32_t, XpUiRect, XpUiRect);
typedef void (*ChildFonts)(void);
typedef void (*ChildPrewarm)(int32_t, const void*, XpEpubChildRowText, uint32_t);

CONTRACT(offsetof(XpEpubLeaves, clipping_physical) == 152 * sizeof(void (*)(void)));
CONTRACT(sizeof(XpEpubLeaves) == 158 * sizeof(void (*)(void)));
CONTRACT(offsetof(XpEpubLeaves, child_list_theme) == 132 * sizeof(void (*)(void)));
CONTRACT(offsetof(XpEpubLeaves, child_utility_layout) == 133 * sizeof(void (*)(void)));
CONTRACT(offsetof(XpEpubLeaves, child_header) == 134 * sizeof(void (*)(void)));
CONTRACT(offsetof(XpEpubLeaves, child_utility_footer) == 135 * sizeof(void (*)(void)));
CONTRACT(offsetof(XpEpubLeaves, child_qr_draw) == 136 * sizeof(void (*)(void)));
CONTRACT(offsetof(XpEpubLeaves, child_toolbar_icon) == 137 * sizeof(void (*)(void)));
CONTRACT(offsetof(XpEpubLeaves, child_clear_fonts) == 138 * sizeof(void (*)(void)));
CONTRACT(offsetof(XpEpubLeaves, child_prewarm_rows) == 139 * sizeof(void (*)(void)));
SIGNATURE(child_list_theme, ChildTheme);
SIGNATURE(child_utility_layout, ChildLayout);
SIGNATURE(child_header, ChildHeader);
SIGNATURE(child_utility_footer, ChildFooter);
SIGNATURE(child_qr_draw, ChildQr);
SIGNATURE(child_toolbar_icon, ChildIcon);
SIGNATURE(child_clear_fonts, ChildFonts);
SIGNATURE(child_prewarm_rows, ChildPrewarm);
SIGNATURE(clipping_physical, const XpClippingPhysical*);

CONTRACT(offsetof(XpEpubLeaves, child_list_help) == 153 * sizeof(void (*)(void)));
CONTRACT(offsetof(XpEpubLeaves, child_list_icon) == 154 * sizeof(void (*)(void)));
typedef void (*ChildListHelp)(XpUiRect, XpAppText);
typedef void (*ChildListBookmark)(uint8_t, XpUiRect, XpUiRect, uint8_t);
SIGNATURE(child_list_help, ChildListHelp);
SIGNATURE(child_list_icon, ChildListBookmark);

#undef SIGNATURE
#undef CONTRACT
