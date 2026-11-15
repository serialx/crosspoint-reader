#include <Xtc/RustXtcParser.h>
#include <Xtc/XtcParser.h>

#include <type_traits>

static_assert(std::is_same_v<xtc::XtcParser, xtc::RustXtcParser>);
static_assert(
    std::is_same_v<decltype(std::declval<xtc::XtcParser&>().getChapters()), std::span<const XpXtcChapterRow>>);
int main() { return 0; }
