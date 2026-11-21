#pragma once

#include <string>
#include <string_view>

// Convert an HTML fragment to plain text for the dictionary's plain-text
// fallback. Styling is ignored: block elements become line breaks and HTML
// entities are decoded. A heading becomes a label line of its own: the
// headingMarker byte is written at the start of that line, and the definition
// view draws a line starting with it in bold.
std::string htmlToPlainText(std::string_view html, char headingMarker);
