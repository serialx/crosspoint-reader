#include <stddef.h>
#include <xp_xtc_owner.h>

_Static_assert(sizeof(XpBookXtcState) == 68, "XTC state layout");
_Static_assert(sizeof(XpXtcOwnerStorage) == (sizeof(void*) == 4 ? 160 : 208), "owner storage");
_Static_assert(sizeof(XpXtcChapterRow) == (sizeof(void*) == 4 ? 28 : 40), "chapter stride");
_Static_assert(offsetof(XpXtcChapterRow, inline_name) == 2 * sizeof(void*), "inline name offset");
_Static_assert(offsetof(XpXtcChapterRow, start_page) == 2 * sizeof(void*) + 16, "page offset");
_Static_assert(sizeof(XpXtcView) == 7 * sizeof(void*), "bulk snapshot");
_Static_assert(sizeof(XpXtcLeaves) == 9 * sizeof(void*), "physical leaves");
_Static_assert(sizeof(XpXtcStream) == 2 * sizeof(void*), "stream sink");
int main(void) { return 0; }
