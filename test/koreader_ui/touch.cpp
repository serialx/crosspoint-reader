#include <xp_ui_koreader.h>

#include <cassert>
#include <vector>

#include "../ui_settings/Recorder.h"
using App = ui::FreeInkApp<24, 6>;
struct Context {
  App* app;
  unsigned calls = 0;
  int value = -1;
};
int main() {
  Recorder target;
  ui::DeviceContext device;
  device.width = 528;
  device.height = 792;
  device.hasTouch = true;
  App app(target, device);
  Context context{&app};
  app.on(
      1,
      [](const ui::ActionEvent& e, void* p) {
        auto& c = *static_cast<Context*>(p);
        c.app->clearTapFlash();
        ++c.calls;
        c.value = e.value;
      },
      &context);
  app.setScreen([](App::ScreenType& screen, void*) {
    ui::ListItem rows[] = {{"Apply remote"}, {"Upload local"}};
    rows[1].actionValue = 1;
    ui::ListProps props;
    props.items = rows;
    props.count = 2;
    props.action = 1;
    props.inputMask = ui::InputTouch;
    screen.list(props);
  });
  for (int targetRow : {0, 1})
    for (bool press : {false, true})
      for (bool inside : {false, true}) {
        app.render();
        context.calls = 0;
        context.value = -1;
        XpKoState state{};
        state.phase = XP_KO_RESULT;
        int y = -1;
        for (int probe = 0; probe < 300; ++probe) {
          ui::Interaction h;
          if (app.hitPublished(40, probe, 1, h) && h.value == targetRow) {
            y = probe + 1;
            break;
          }
        }
        assert(y >= 0);
        auto route = [&](bool down, bool up, int x, int yy) {
          ui::InputSnapshot old;
          old.touchX = x;
          old.touchY = yy;
          old.touchPressed = down;
          old.touchReleased = up;
          const auto event = app.route(old);
          XpUiInput next{};
          next.touch_x = x;
          next.touch_y = yy;
          next.touch_flags = (down ? XP_UI_TOUCH_PRESSED : 0) | (up ? XP_UI_TOUCH_RELEASED : 0);
          ui::Interaction hit;
          if (app.hitPublished(x, yy, 1, hit)) {
            next.action = 1;
            next.value = hit.value;
          }
          const auto effect = xp_ui_koreader_update(&state, &next);
          assert(bool(event) == (effect.kind == XP_UI_ACTIVATE));
          if (event) assert(effect.value == event.value);
          // The family adapter defers this repaint until its synchronous handler returns.
          assert(app.invalidated() == bool(effect.invalidate || effect.kind == XP_UI_ACTIVATE));
        };
        if (press) route(true, false, 40, y);
        route(false, true, inside ? 40 : -1, inside ? y : -1);
        assert(context.calls == unsigned(inside));
      }
}
