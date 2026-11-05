"""Production-source mutants for test_child_handle_composition.fixture.

Each yielded dict supplies name, native, owner, manager, case, flag and
expected_rejection. Pass its three source strings to fixture(), then run the
compiled binary with case and flag. Compilation failure is never a rejection.
The runner must retain stderr and require the indicated runtime failure class.
This module neither compiles nor executes candidates.
"""

from test_app_results import block
from test_popup_resource import native_leaf, replace_once


# Each mode reaches the named production factory through the existing real App.
FACTORY_CASES = {
    'menu': 'menu',
    'chapter': 'chapter',
    'percent': 'percent',
    'text_settings': 'text',
    'footnote': 'footnote',
    'dictionary': 'dictionary',
    'selection': 'clip',
    'bookmarks': 'bookmarks',
    'clippings': 'clippings',
    'qr': 'qr',
    'wifi': 'wifi',
    'sync': 'sync-replace',
}

# These are runtime classes, not permission to accept any nonzero process exit.
# The lifetime class permits the physical Page destructor's exact-once assertion
# as well as the sanitizer detecting its freed storage before that assertion.
SANITIZER = 'asan-or-ubsan'
PAGE_LIFETIME = 'page-lifetime-assertion-or-asan-or-ubsan'
INVALID_DOWNCAST = 'ubsan-invalid-reader-downcast'


def _leaf_change(source, name, before, after):
    original = native_leaf(source, name)
    changed = replace_once(original, before, after)
    return replace_once(source, original, changed)


def mutants(native, owner, manager):
    """Yield 22 independently mutated source/case records, without side effects.

    Failure stages are the fixture's zero-based nothrow allocation indices.
    Stage 1 in the reversed helper is wrapper OOM after a constructed child;
    stage 1 with the ordinary helper is child OOM after an empty wrapper.
    Existing result-order/intent mutants remain in the App result test driver.
    """
    def record(name, case, rejection, *, native_source=native,
               owner_source=owner, manager_source=manager):
        return dict(name=name, native=native_source, owner=owner_source,
                    manager=manager_source, case=case, flag='0',
                    expected_rejection=rejection)

    for leaf, case in FACTORY_CASES.items():
        changed = _leaf_change(native, 'child_' + leaf,
                               'makeLegacyActivity<', 'makeUniqueNoThrow<')
        yield record('raw-child-' + leaf, case, SANITIZER,
                     native_source=changed)

    signature = 'std::unique_ptr<OwnedLegacyActivity> makeLegacyActivity(Args&&... args)'
    original = block(owner, signature)
    reversed_helper = signature + ''' {
  auto activity = makeUniqueNoThrow<T>(std::forward<Args>(args)...);
  if (!activity) return nullptr;
  return ownLegacy(std::move(activity));
}'''
    changed_owner = replace_once(owner, original, reversed_helper)
    for leaf in ('selection', 'footnote', 'dictionary'):
        yield record('child-first-wrapper-oom-' + leaf,
                     'factory-fail-' + leaf + '-1', PAGE_LIFETIME,
                     owner_source=changed_owner)

    for leaf in ('selection', 'footnote', 'dictionary'):
        release = ('for (auto& page : pages) page.release();'
                   if leaf == 'selection' else 'if (!child) page.release();')
        changed = _leaf_change(native, 'child_' + leaf, release, '(void)child;')
        yield record('consume-page-on-failure-' + leaf,
                     'factory-fail-' + leaf + '-1', PAGE_LIFETIME,
                     native_source=changed)

    changed_facts = _leaf_change(
        manager, 'facts',
        'auto* reader = input ? activity->readerControls() : nullptr;',
        'auto* reader = input && owner.reader ? static_cast<ReaderActivity*>(activity) : nullptr;')
    for kind in ('selection', 'interval'):
        yield record('policy-downcast-facts-' + kind, 'capability-' + kind,
                     INVALID_DOWNCAST, manager_source=changed_facts)

    # Keep facts safe here so the remote-turn downcast is reached independently.
    changed_turn = _leaf_change(
        manager, 'reader_turn',
        'auto* reader = nativeActivity(activity)->readerControls();',
        'auto* reader = nativeActivity(activity)->isReaderActivity()\n'
        '                           ? static_cast<ReaderActivity*>(nativeActivity(activity))\n'
        '                           : nullptr;')
    for kind in ('selection', 'interval'):
        yield record('policy-downcast-reader-turn-' + kind, 'capability-' + kind,
                     INVALID_DOWNCAST, manager_source=changed_turn)
