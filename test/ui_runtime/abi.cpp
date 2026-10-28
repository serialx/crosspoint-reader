#include <xp_ui.h>

#include <array>
#include <cassert>
#include <cstring>

static_assert(sizeof(XpUiListState) == 36);
static_assert(sizeof(XpUiListStyle) == 40);
static_assert(sizeof(XpUiXtcLayout) == 72);
static_assert(sizeof(XpUiInput) == 36);

int main() {
  XpUiListState state{};
  const XpUiResources resources{nullptr,
                                [](void*, uint32_t handle, uint16_t* length) -> const uint8_t* {
                                  const char* text = handle < XP_UI_XTC_TITLE ? "Chapter" : "Label";
                                  *length = strlen(text);
                                  return reinterpret_cast<const uint8_t*>(text);
                                },
                                [](void*, uint16_t i, uint32_t* start, uint32_t* end) {
                                  *start = i * 3;
                                  *end = *start + 2;
                                },
                                100,
                                1,
                                0,
                                7};
  assert(xp_ui_list_init(&state, &resources, 17));
  assert(state.selected == 5 && state.follow == 1 && state.active == -1);
  XpUiInput input{};
  input.pressed = 4;
  auto effect = xp_ui_list_update(&state, &input, 100, 1);
  assert(effect.invalidate && state.selected == 6);
  input = {};
  input.released = 2;
  effect = xp_ui_list_update(&state, &input, 100, 1);
  assert(effect.kind == XP_UI_ACTIVATE && effect.value == 6);
  const XpUiMetrics metrics{nullptr,
                            [](void*, int32_t, const uint8_t*, uint16_t len, uint8_t) -> int32_t { return len * 8; },
                            [](void*, int32_t) -> int32_t { return 24; }};
  XpUiXtcLayout layout{};
  layout.screen = {0, 0, 480, 800};
  layout.safe = {0, 0, 480, 750};
  layout.list.screen = layout.screen;
  layout.list.min_touch = 44;
  layout.list.row_height = 32;
  layout.list.padding = 20;
  layout.list.scroll_width = 4;
  layout.header_height = 50;
  layout.spacing = 8;
  std::array<XpUiCommand, 80> commands{};
  std::array<XpUiInteraction, 32> hits{};
  XpUiBuffer buffer{commands.data(), hits.data(), 80, 32};
  XpUiScene scene{};
  XpUiListState next{};
  assert(xp_ui_xtc_view(&state, &layout, &metrics, &resources, &buffer, &next, &scene));
  assert(scene.command_count < 80 && scene.interaction_count == 21);
  assert(scene.refresh == XP_UI_FAST && scene.generation == 7);
  assert(commands[0].kind == XP_UI_CLEAR && commands[1].kind == XP_UI_TEXT_HANDLE);
  XpUiInteraction hit{};
  assert(xp_ui_hit(&scene, 40, 70, 1, &hit) && hit.value == 0);
  assert(!xp_ui_hit(&scene, -1, -1, 1, &hit));
  const auto oldState = state;
  const auto oldScene = scene;
  buffer.command_capacity = 1;
  assert(!xp_ui_xtc_view(&state, &layout, &metrics, &resources, &buffer, &next, &scene));
  assert(!memcmp(&state, &oldState, sizeof(state)) && !memcmp(&scene, &oldScene, sizeof(scene)));
  assert(!xp_ui_xtc_view(nullptr, &layout, &metrics, &resources, &buffer, &next, &scene));
  assert(!xp_ui_message_view(nullptr, layout.screen, 0, 0, nullptr, 0, commands.data(), 80));
  return 0;
}
