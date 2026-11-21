"""Hold an SD glyph loan across the cache release on both retry routes."""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'test/reader_lifecycle'))
from extract import definition

source = (ROOT / 'src/activities/settings/FontDownloadActivity.cpp').read_text()
callbacks = []
for name in ('fetchAndParseManifest', 'downloadFamily'):
    body = definition(source, 'FontDownloadActivity::' + name + '(')
    # Exercise the cache block before TLS, keeping its production lock scope.
    callbacks.append(definition(body, 'if (auto* fcm = renderer.getFontCacheManager())'))
registry = definition(source, 'void FontDownloadActivity::refreshFontRegistry()')
fixture = r'''
#include <atomic>
#include <cassert>
#include <future>
#include <mutex>
#define LOG_DBG(...) ((void)0)
struct FontDownloadActivity {
  std::mutex panel;
  std::promise<void> attempted;
  std::atomic<bool> loan = true;
  unsigned releases = 0;
  struct RenderLock {
    std::unique_lock<std::mutex> held;
    explicit RenderLock(FontDownloadActivity& activity) {
      activity.attempted.set_value();
      held = std::unique_lock(activity.panel);
    }
  };
  struct Cache {
    FontDownloadActivity& activity;
    void releaseSdFontCaches() {
      assert(!activity.loan);
      ++activity.releases;
    }
    Cache* getFontCacheManager() { return this; }
    void refreshRegistry() { releaseSdFontCaches(); }
  } renderer{*this}, fontInstaller_{*this};
  void refreshFontRegistry();
  void manifest() { MANIFEST }
  void family() { FAMILY }
};
REGISTRY
int main() {
  for (auto retry : {&FontDownloadActivity::manifest, &FontDownloadActivity::family, &FontDownloadActivity::refreshFontRegistry}) {
    FontDownloadActivity activity;
    std::unique_lock paint(activity.panel);
    auto attempted = activity.attempted.get_future();
    auto worker = std::async(std::launch::async, [&] { (activity.*retry)(); });
    attempted.get();
    assert(activity.releases == 0);
    activity.loan = false;
    paint.unlock();
    worker.get();
    assert(activity.releases == 1 && activity.panel.try_lock());
    activity.panel.unlock();
  }
}
'''.replace('MANIFEST', callbacks[0]).replace('FAMILY', callbacks[1]).replace('REGISTRY', registry)
with tempfile.TemporaryDirectory() as directory:
    cpp = Path(directory) / 'retry.cpp'
    binary = cpp.with_suffix('')
    cpp.write_text(fixture)
    subprocess.run(['c++', '-std=c++20', str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=15)
