#pragma once

#include <components/lists/list.h>
#include <xp_plugin.h>

// Call after resolving the list's theme geometry and font slots.
inline void fitPluginSubtitleToViewport(const freeink::ui::DrawTarget& target, const int viewportHeight,
                                        freeink::ui::ListProps& props) {
  props.subtitleText.maxLines = xp_plugin_subtitle(
      viewportHeight, props.labelText.maxLines, target.lineHeight(props.labelText.font),
      target.lineHeight(props.valueText.font), target.lineHeight(props.subtitleText.font), props.rowPaddingY);
}
