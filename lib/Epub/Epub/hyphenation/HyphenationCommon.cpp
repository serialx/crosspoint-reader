#include "HyphenationCommon.h"

bool isExplicitHyphen(const uint32_t cp) {
  switch (cp) {
    case '-':
    case 0x00AD:  // soft hyphen
    case 0x058A:  // Armenian hyphen
    case 0x2010:  // hyphen
    case 0x2011:  // non-breaking hyphen
    case 0x2012:  // figure dash
    case 0x2013:  // en dash
    case 0x2014:  // em dash
    case 0x2015:  // horizontal bar
    case 0x2043:  // hyphen bullet
    case 0x207B:  // superscript minus
    case 0x208B:  // subscript minus
    case 0x2212:  // minus sign
    case 0x2E17:  // double oblique hyphen
    case 0x2E3A:  // two-em dash
    case 0x2E3B:  // three-em dash
    case 0xFE58:  // small em dash
    case 0xFE63:  // small hyphen-minus
    case 0xFF0D:  // fullwidth hyphen-minus
    case 0x005F:  // Underscore
    case 0x2026:  // Ellipsis
      return true;
    default:
      return false;
  }
}

bool isSoftHyphen(const uint32_t cp) { return cp == 0x00AD; }
