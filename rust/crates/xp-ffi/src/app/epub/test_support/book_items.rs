use super::*;

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_items(
    resources: &'static abi::Resources,
    leaves: &'static XpEpubLeaves,
    path: abi::Text,
    href: abi::Text,
    kind: u8,
    sink: Sink,
    context: *mut c_void,
) -> i64 {
    unsafe extern "C" fn now() -> u32 {
        0
    }
    let Some(path) = (unsafe { path.bytes() }) else {
        return -1;
    };
    let Some(href) = (unsafe { href.bytes() }) else {
        return -1;
    };
    let Ok(path) = Text::copy(path) else {
        return -1;
    };
    let Ok(protection) = Protection::open(leaves, &path) else {
        return -1;
    };
    let items = Items {
        path: &path,
        protection: &protection,
        resources,
        leaves,
        now,
    };
    match kind {
        0 => items.size(href).map_or(-1, i64::from),
        1 | 2 => i64::from(unsafe { items.stream(href, 1024, kind == 2, sink, context) }),
        3 | 4 => items.memory(href, kind == 4).map_or(-1, |contents| {
            let bytes = contents.bytes();
            if kind == 4 {
                assert_eq!(unsafe { *contents.buffer.as_ptr().add(bytes.len()) }, 0);
            }
            unsafe { sink(context, bytes.as_ptr(), bytes.len()) };
            bytes.len() as i64
        }),
        _ => -1,
    }
}
