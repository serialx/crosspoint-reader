use crate::menu::{Flags, Items};
use xp_ui::reader::menu_parent::Action;

#[test]
fn all_menu_presence_combinations_preserve_the_native_order() {
    for bits in 0..128 {
        let flags = Flags {
            footnotes: bits & 1 != 0,
            bookmarks: bits & 2 != 0,
            clippings: bits & 4 != 0,
            frontlight: bits & 8 != 0,
            bluetooth: bits & 16 != 0,
            dictionary: bits & 32 != 0,
            sync: bits & 64 != 0,
        };
        let items = Items::new(flags);
        let mut expected = vec![Action::Chapter as u8];
        for (enabled, action) in [
            (flags.footnotes, Action::Footnotes),
            (flags.bookmarks, Action::Bookmarks),
            (true, Action::ToggleBookmark),
            (true, Action::SaveClipping),
            (flags.clippings, Action::ViewClippings),
            (true, Action::Night),
            (flags.frontlight, Action::Frontlight),
            (flags.bluetooth, Action::Bluetooth),
            (flags.dictionary, Action::Dictionary),
            (true, Action::Orientation),
            (true, Action::Auto),
            (true, Action::Percent),
            (true, Action::Screenshot),
            (true, Action::Qr),
            (true, Action::Home),
            (flags.sync, Action::Sync),
            (true, Action::DeleteCache),
            (true, Action::Text),
        ] {
            if enabled {
                expected.push(action as u8)
            }
        }
        assert_eq!(
            items.actions().iter().map(|a| *a as u8).collect::<Vec<_>>(),
            expected
        );
        for index in 0..items.actions().len() {
            assert_ne!(items.label(index), None);
        }
        assert_eq!(items.label(items.actions().len()), None);
    }
}
