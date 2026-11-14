// Hooks that make_variant.py inserts into generated *_observed decoder copies. Firmware has no hook.
#pragma once

void jhg_hit(const char* site);
extern int jhg_limit;  // end of the current segment's bytes in the buffer: min(segment end, bytes read)
extern int jhg_sos;    // last JPEGGetSOS result

#define JHG_OBSERVE(site, cond) \
  do {                          \
    if (cond) jhg_hit(site);    \
  } while (0)
