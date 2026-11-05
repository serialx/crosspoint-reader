#include <xp_ui_reader.h>

#include <cassert>
#include <chrono>
#include <cstring>
#include <future>
#include <mutex>
#include <string>
#include <type_traits>

struct ReaderSession {
  XpUiReaderSession state;
  explicit ReaderSession(const XpUiReaderSession& value) : state(value) {}
};
uint32_t millis() { return 10000; }
namespace trustedtime {
int64_t trustedNow() { return 110; }
}  // namespace trustedtime
struct ReaderActivity {
  std::mutex mutex, panel;
  struct ModelLock {
    std::lock_guard<std::mutex> guard;
    explicit ModelLock(ReaderActivity& owner) : guard(owner.mutex) {}
  };
  struct RenderLock {};
  XpUiReaderBaseModel baseModel{};
  XpUiReaderBaseModel* renderFrame = nullptr;
  std::promise<void> painting, resume;
  uint8_t runBase(uint8_t, uint8_t, uint8_t) {
    // Keep the panel occupied while actual input helpers mutate the live model.
    const std::lock_guard<std::mutex> busy(panel);
    painting.set_value();
    resume.get_future().wait();
    renderRefreshCounter() = 14;
    completeReaderSession();
    markPageRendered();
    return 0;
  }
  int getProgressBasisPoints() const { return 20; }
  void render(RenderLock&&);
  uint32_t readBaseField(uint8_t) const;
  void writeBaseField(uint8_t, uint32_t, bool = false);
  ReaderSession snapshotReaderSession() const;
  void resetReaderSession();
  void completeReaderSession();
  void queueForcedRefresh();
  void setRefreshCounter(int32_t);
  int32_t& renderRefreshCounter();
  bool takeForcedRefresh();
  void markPageRendered();
  bool acceptsRemoteInput() const;
  void notePageTurn(bool, bool);
};
// BASE_PUBLICATION_METHODS

int main(int argc, char** argv) {
  ReaderActivity reader;
  if (argc == 2 && std::string(argv[1]) == "nested") {
    reader.renderFrame = &reader.baseModel;
    reader.render({});
    return 0;
  }
  for (int counter : {-1, 0, 1}) {
    for (bool manual : {false, true}) {
      ReaderActivity r;
      xp_ui_reader_base_init(&r.baseModel, 1, 15);
      xp_ui_reader_session_render(&r.baseModel.session, 0, 100, 10);
      r.notePageTurn(true, true);
      auto paint = std::async(std::launch::async, [&] { r.render({}); });
      r.painting.get_future().wait();
      auto input = std::async(std::launch::async, [&] {
        r.notePageTurn(true, true);
        if (manual) r.queueForcedRefresh();
        if (counter >= 0) r.setRefreshCounter(counter);
        r.writeBaseField(XP_UI_READER_BASE_FIELD_REMEMBERED, 1);
        assert(!r.acceptsRemoteInput());
      });
      assert(input.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
      input.get();
      r.resume.set_value();
      paint.get();
      assert(!r.renderFrame);
      assert(r.acceptsRemoteInput());
      assert(r.baseModel.session.duration == 10);
      assert(r.baseModel.session.pending_forward == 1);
      assert(r.baseModel.book_remembered == 1);
      const int expectedCounter = counter >= 0 ? counter : manual ? 1 : 14;
      assert(r.baseModel.pages_until_full_refresh == expectedCounter);
      assert(r.baseModel.refresh_requests == (manual ? 1 : 0) + (counter >= 0 ? 2 : 0));
      XpUiReaderBaseModel next{};
      xp_ui_reader_base_begin_render(&r.baseModel, &next);
      r.renderFrame = &next;
      assert(next.session.pending_forward == 1);
      assert(r.takeForcedRefresh() == manual);
      assert(!r.takeForcedRefresh());
      assert(r.renderRefreshCounter() == expectedCounter);
      r.renderRefreshCounter() = 9;
      xp_ui_reader_session_render(&next.session, 20000, 120, 30);
      xp_ui_reader_base_publish(&r.baseModel, &next);
      r.renderFrame = nullptr;
      assert(r.baseModel.pages_until_full_refresh == 9);
      assert(!r.baseModel.refresh_requests && !r.baseModel.session.pending_forward);
      r.resetReaderSession();
      const XpUiReaderSession empty{};
      const auto session = r.snapshotReaderSession();
      assert(!std::memcmp(&session.state, &empty, sizeof(empty)));
    }
  }
  XpUiReaderBaseModel state{}, frame{};
  xp_ui_reader_base_init(&state, 1, 15);
  const auto before = state;
  xp_ui_reader_base_begin_render(nullptr, &frame);
  xp_ui_reader_base_begin_render(&state, nullptr);
  xp_ui_reader_base_publish(nullptr, &frame);
  xp_ui_reader_base_publish(&state, nullptr);
  xp_ui_reader_base_force_refresh(nullptr);
  xp_ui_reader_base_model_write(nullptr, 0, 1);
  xp_ui_reader_base_init(nullptr, 1, 15);
  assert(!xp_ui_reader_base_model_read(nullptr, 0));
  assert(!std::memcmp(&state, &before, sizeof(state)));
}
