#include "Activity.h"

#include "ActivityManager.h"

Activity::Activity(std::string name, GfxRenderer& renderer, MappedInputManager& mappedInput)
    : name(std::move(name)), renderer(renderer), mappedInput(mappedInput) {}

Activity::~Activity() = default;

void Activity::onEnter() { LOG_DBG("ACT", "Entering activity: %s", name.c_str()); }

void Activity::onExit() { LOG_DBG("ACT", "Exiting activity: %s", name.c_str()); }

void Activity::requestUpdate(bool immediate) { activityManager.requestUpdate(immediate); }

void Activity::requestUpdateAndWait() { activityManager.requestUpdateAndWait(); }

void Activity::onGoHome(HomeMenuItem item) { activityManager.goHome(item); }

void Activity::onSelectBook(const std::string& path) { activityManager.goToReader(path); }

namespace {
__attribute__((noinline)) void cancelRejectedDialog(Activity& parent, ActivityResultHandler& slot) {
  // Rejection has destroyed the child; the published parent remains current.
  auto rejected = std::move(slot);
  slot = nullptr;
  ActivityResult cancelled;
  cancelled.isCancelled = true;
  if (rejected) rejected(cancelled);
  parent.requestUpdate();
}
}  // namespace

void Activity::startActivityForResult(std::unique_ptr<Activity>&& activity, ActivityResultHandler resultHandler) {
  const bool dialog = activity && activity->dialog();
  this->resultHandler = std::move(resultHandler);
  const bool accepted = activityManager.pushActivity(std::move(activity));
  if (dialog && !accepted) cancelRejectedDialog(*this, this->resultHandler);
}

void Activity::setResult(ActivityResult&& result) { this->result = std::move(result); }

void Activity::finish() { activityManager.popActivity(); }
