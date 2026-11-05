use super::*;

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_txt_build(
    resources: &'static abi::Resources,
    leaves: &'static XpEpubLeaves,
    path: abi::Text,
    cache_path: abi::Text,
) -> u8 {
    let (Some(path), Some(cache_path)) = (unsafe { path.bytes() }, unsafe { cache_path.bytes() })
    else {
        return 0;
    };
    let (Ok(path), Ok(cache_path)) = (Text::copy(path), Text::copy(cache_path)) else {
        return 0;
    };
    let mut cache = super::super::BookCache::new(leaves);
    let mut metadata = super::super::Metadata::default();
    u8::from(build(
        &path,
        &cache_path,
        &mut cache,
        &mut metadata,
        resources,
        leaves,
        &xp_core::epub::diagnostic::SILENT,
    ))
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_txt_stream(
    resources: &'static abi::Resources,
    leaves: &'static XpEpubLeaves,
    path: abi::Text,
    sink: Sink,
    context: *mut c_void,
) -> u8 {
    let Some(path) = (unsafe { path.bytes() }) else {
        return 0;
    };
    let Ok(path) = Text::copy(path) else {
        return 0;
    };
    unsafe extern "C" fn now() -> u32 {
        0
    }
    u8::from(unsafe { stream(&path, resources, leaves, now, sink, context) })
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_txt_validate(
    resources: &'static abi::Resources,
    leaves: &'static XpEpubLeaves,
    path: abi::Text,
    cache: abi::Text,
    cached_size: u32,
) -> u8 {
    let (Some(path), Some(cache)) = (unsafe { path.bytes() }, unsafe { cache.bytes() }) else {
        return 0;
    };
    let (Ok(path), Ok(cache)) = (Text::copy(path), Text::copy(cache)) else {
        return 0;
    };
    u8::from(validate(&path, &cache, cached_size, resources, leaves))
}
