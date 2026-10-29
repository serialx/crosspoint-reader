#include <xp_ui.h>

#include <array>
#include <cassert>
#include <cstring>

static_assert(sizeof(XpUiListState) == 40);
static_assert(sizeof(XpUiListStyle) == 40);
static_assert(sizeof(XpUiXtcLayout) == 72);
static_assert(sizeof(XpUiInput) == 36);
static_assert(sizeof(XpUiInterest) == 24);

static void serviceClip() {
  struct Calls {
    int clear = 0;
    int text = 0;
    int resource = 0;
  } calls;
  const XpUiPaint paint{&calls, [](void* p, const XpGlyphTarget*) { ++static_cast<Calls*>(p)->clear; },
                        [](void* p, const XpGlyphTarget*, const XpUiCommand*, XpUiRect clip, const char*) {
                          assert(clip.x == 2 && clip.y == 1 && clip.w == 4 && clip.h == 1);
                          ++static_cast<Calls*>(p)->text;
                        },
                        [](void*, uint32_t, uint32_t) -> uint8_t { return 1; },
                        [](void* p, const XpGlyphTarget*, const XpUiCommand*, XpUiRect clip) {
                          assert(clip.x == 2 && clip.y == 1 && clip.w == 4 && clip.h == 1);
                          ++static_cast<Calls*>(p)->resource;
                        }};
  XpUiCommand commands[3]{};
  commands[0].kind = XP_UI_CLEAR;
  commands[1].kind = XP_UI_TEXT;
  commands[1].text_len = 1;
  commands[2].kind = XP_UI_BITMAP;
  commands[2].resource = 1;
  const uint8_t text[] = {'x', 0};
  const XpUiScene scene{commands, text, nullptr, 0, 3, 2, 0, XP_UI_NONE, 0};
  uint8_t bytes[2]{};
  XpGlyphTarget target{bytes, 2, 8, 2, 1, 0, 2, {2, 1, 6, 2}, 3, 0, 0, 0, 0};
  assert(xp_ui_render(&scene, &target, &paint));
  assert(calls.clear == 1 && calls.text == 1 && calls.resource == 1);
  target.clip = {0, 0, 0, 0};
  assert(xp_ui_render(&scene, &target, &paint));
  assert(calls.clear == 2 && calls.text == 1 && calls.resource == 1);
}

static void layerOwners() {
  struct Owner {
    uint8_t pixels;
    bool valid;
  } owners[] = {{0x55, true}, {0xAA, true}};
  const auto paint = [](Owner* owner) {
    return XpUiPaint{
        owner, nullptr, nullptr,
        [](void* p, uint32_t h, uint32_t g) -> uint8_t { return h == 1 && g == 1 && static_cast<Owner*>(p)->valid; },
        [](void* p, const XpGlyphTarget* t, const XpUiCommand* c, XpUiRect) {
          t->buffer[c->rect.y * t->stride] = static_cast<Owner*>(p)->pixels;
        }};
  };
  auto a = paint(&owners[0]), b = paint(&owners[1]);
  XpUiCommand commands[2]{};
  for (auto& c : commands) {
    c.kind = XP_UI_BITMAP;
    c.resource = 1;
    c.generation = 1;
    c.rect = {0, 0, 8, 1};
  }
  commands[1].rect.y = 1;
  const XpUiScene scenes[] = {{commands, nullptr, nullptr, 1, 1, 0, 0, XP_UI_NONE, 0},
                              {commands + 1, nullptr, nullptr, 1, 1, 0, 0, XP_UI_NONE, 0}};
  const XpUiLayer layers[] = {{&scenes[0], &a}, {&scenes[1], &b}};
  uint8_t bytes[2]{};
  XpGlyphTarget target{bytes, 2, 8, 2, 1, 0, 2, {0, 0, 8, 2}, 3, 0, 0, 0, 0};
  assert(xp_ui_render_layers(layers, 2, &target));
  assert(bytes[0] == 0x55 && bytes[1] == 0xAA);
  memset(bytes, 0xCC, sizeof(bytes));
  owners[1].valid = false;
  assert(!xp_ui_render_layers(layers, 2, &target));
  assert(bytes[0] == 0xCC && bytes[1] == 0xCC);
  const XpUiLayer invalid[] = {layers[0], {&scenes[1], nullptr}};
  assert(!xp_ui_render_layers(invalid, 2, &target));
  assert(bytes[0] == 0xCC && bytes[1] == 0xCC);
}

int main() {
  serviceClip();
  layerOwners();
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
