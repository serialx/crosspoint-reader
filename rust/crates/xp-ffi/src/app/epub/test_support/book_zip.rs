use super::*;

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_zip_diagnostic(
    leaves: &XpEpubLeaves,
    name: abi::Text,
    event: u32,
    a: u32,
    b: u32,
) {
    diagnostics::message(leaves, unsafe { name.bytes() }.unwrap(), event, a, b);
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_zip(
    resources: &'static abi::Resources,
    leaves: &'static XpEpubLeaves,
    path: abi::Text,
    name: abi::Text,
    kind: u8,
    sink: Sink,
    context: *mut c_void,
) -> i64 {
    let Some(path) = (unsafe { path.bytes() }) else {
        return -1;
    };
    let Some(name) = (unsafe { name.bytes() }) else {
        return -1;
    };
    let Ok(path) = Text::copy(path) else {
        return -1;
    };
    let mut archive = Archive::new(&path, resources, leaves);
    match kind {
        0 => archive
            .lookup(name)
            .map_or(-1, |entry| i64::from(entry.uncompressed_size)),
        1 | 2 => i64::from(unsafe { archive.stream(name, 1024, kind == 2, sink, context) }),
        3 => i64::from(archive.enumerate(|name, _| {
            unsafe { sink(context, name.as_ptr(), name.len()) };
        })),
        4 => {
            if !archive.open() || archive.lookup(name).is_none() || archive.lookup(name).is_none() {
                return 0;
            }
            i64::from(unsafe { archive.stream(name, 1024, false, sink, context) })
        }
        5 | 6 => archive.memory(name, kind == 6).map_or(0, |bytes| {
            unsafe { sink(context, bytes.as_ptr(), bytes.len()) };
            1
        }),
        _ => -1,
    }
}
