#pragma once
#include <Logging.h>
#include <xp_ffi.h>

#include <algorithm>
#include <climits>

// Event payloads are defined beside XpPkgDiagnostics. Only C++ formats logs.
inline void packageDiagnostic(void*, uint32_t event, uint32_t a, uint32_t, uint32_t, uint32_t, const uint8_t* text,
                              uint32_t size, const uint8_t* other, uint32_t otherSize) {
  const auto* value = reinterpret_cast<const char*>(text);
  const auto* second = reinterpret_cast<const char*>(other);
  const int len = static_cast<int>(std::min(size, static_cast<uint32_t>(INT_MAX)));
  const int secondLen = static_cast<int>(std::min(otherSize, static_cast<uint32_t>(INT_MAX)));
  switch (event) {
    case XP_PKG_DIAG_NAV_BEGIN:
      LOG_DBG("NAV", "Found nav toc element");
      break;
    case XP_PKG_DIAG_NAV_END:
      LOG_DBG("NAV", "Finished parsing nav toc");
      break;
    case XP_PKG_DIAG_METADATA_TRUNCATED:
      LOG_DBG("COF", "Metadata text exceeds %u bytes; truncating", static_cast<unsigned>(a));
      break;
    case XP_PKG_DIAG_COLLECTION_IGNORED:
      LOG_DBG("COF", "Ignoring collection metadata beyond %u entries", static_cast<unsigned>(a));
      break;
    case XP_PKG_DIAG_COVER_IGNORED:
      LOG_DBG("COF", "Ignoring meta cover item '%.*s' with non-image media type: %.*s", len, value, secondLen, second);
      break;
    case XP_PKG_DIAG_NCX_DUPLICATE:
      LOG_DBG("COF", "Warning: Multiple NCX files found in manifest. Ignoring duplicate: %.*s", len, value);
      break;
    case XP_PKG_DIAG_NAV_DOCUMENT:
      LOG_DBG("COF", "Found EPUB 3 nav document: %.*s", len, value);
      break;
    case XP_PKG_DIAG_GUIDE_START:
      LOG_DBG("COF", "Found %s reference in guide: %.*s", "start", len, value);
      break;
    case XP_PKG_DIAG_GUIDE_COVER:
      LOG_DBG("COF", "Found cover reference in guide: %.*s", len, value);
      break;
    case XP_PKG_DIAG_MANIFEST_INDEX:
      LOG_DBG("COF", "Using fast index for %zu manifest items", static_cast<size_t>(a));
      break;
    case XP_PKG_DIAG_GUIDE_BEGIN:
      LOG_DBG("COF", "Entering guide state.");
      break;
    case XP_PKG_DIAG_INDEX_OOM:
      LOG_ERR("BMC", "Failed to allocate spine href index; scanning %d spine items per TOC entry", static_cast<int>(a));
      break;
    case XP_PKG_DIAG_INDEX_READY:
      LOG_DBG("BMC", "Using fast index for %d spine items", static_cast<int>(a));
      break;
    case XP_PKG_DIAG_SPINE_CLOSED:
      LOG_DBG("BMC", "createSpineEntry called but not in build mode");
      break;
    case XP_PKG_DIAG_TOC_CLOSED:
      LOG_DBG("BMC", "createTocEntry called but not in build mode");
      break;
    case XP_PKG_DIAG_MISSING_TOC_HREF:
      LOG_DBG("BMC", "createTocEntry: Could not find spine item for TOC href %.*s", len, value);
      break;
    case XP_PKG_DIAG_MISSING_SPINE_TITLE:
      LOG_DBG("BMC", "Warning: Could not find TOC entry for spine item %d: %.*s, using title from last section",
              static_cast<int>(a), len, value);
      break;
    default:
      break;  // Unknown future diagnostic events are ignored.
  }
}
