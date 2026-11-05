"""Native list touch/swipe routing, frozen independently from the Rust family."""
import gzip
from pathlib import Path
import subprocess
import tempfile
import unittest
from input_oracle import HERE, ROOT, original, method

def record(mutation=False):
    with tempfile.TemporaryDirectory() as directory:
        out = Path(directory)
        leaves=(HERE/'input_leaves.h').read_text().replace('  SwipeDir wasSwipe() const { return swipe; }', '''
  bool hasTouch() const { return true; }
  SwipeDir wasSwipe(int* a=nullptr, int* b=nullptr) const { if(a)*a=x; if(b)*b=y; return swipe; }
''')
        (out/'MappedInputManager.h').write_text(leaves)
        source=(HERE/'touch_leaves.cpp').read_text()
        native=original('src/activities/UiListActivity.cpp')
        for signature in ['void UiListActivity::closeSwipeDelete(', 'bool UiListActivity::handleSwipeDeleteInput(',
                          'void UiListActivity::rowActionTrampoline(', 'void UiListActivity::swipeDeleteTrampoline(',
                          'void UiListActivity::loop(']:
            source+=method(native,signature)
        source+=(HERE/'touch_cases.cpp').read_text()
        if mutation: source=source.replace('swipe == MappedInputManager::SwipeDir::Up ?','swipe == MappedInputManager::SwipeDir::Down ?')
        (out/'touch.cpp').write_text(source)
        sdk=ROOT/'freeink-sdk/libs/ui/FreeInkUI'
        subprocess.run(['c++','-std=c++20','-DFREEINK_CAP_TOUCH=1','-DFREEINK_UI_COMPONENT_STYLE=0',
                        '-I'+str(out),'-I'+str(sdk/'include'),str(out/'touch.cpp'),str(sdk/'src/FreeInkUI.cpp'),
                        '-o',str(out/'touch')],check=True)
        return subprocess.check_output([str(out/'touch')])
class Touch(unittest.TestCase):
    def test_native_routing(self): self.assertEqual(record(),gzip.decompress((HERE/'touch.golden.gz').read_bytes()))
    def test_direction_mutation(self): self.assertNotEqual(record(True),gzip.decompress((HERE/'touch.golden.gz').read_bytes()))
if __name__=='__main__': unittest.main()
