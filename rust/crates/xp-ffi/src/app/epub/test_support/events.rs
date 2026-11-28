use super::super::{abi::XpEpubSessionEvent, app_abi::Text as XpAppText, events};

// Calls the feature-independent shipping payload and resource implementation.
#[no_mangle]
pub(super) unsafe extern "C" fn xp_epub_test_events(
    ports: *const events::Ports,
    kind: u8,
    path: XpAppText,
    document: XpAppText,
    event: *const XpEpubSessionEvent,
    percent: i32,
    end_percent: i32,
) {
    match kind {
        0 => events::open(&*ports, path),
        1 => events::session(&*ports, path, document, &*event),
        2 => events::exit(&*ports, path, percent, end_percent),
        _ => panic!("unexpected fixture event"),
    }
}
