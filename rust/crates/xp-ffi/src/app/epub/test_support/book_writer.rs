use super::*;

#[repr(C)]
pub struct Row {
    href: abi::Text,
    title: abi::Text,
    anchor: abi::Text,
    level: u8,
    toc: u8,
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_writer(
    resources: &'static abi::Resources,
    leaves: &'static XpEpubLeaves,
    path: abi::Text,
    cache_path: abi::Text,
    rows: *const Row,
    count: u32,
) -> u32 {
    let (Some(path), Some(cache_path)) = (unsafe { path.bytes() }, unsafe { cache_path.bytes() })
    else {
        return 1;
    };
    let (Ok(path), Ok(cache_path)) = (Text::copy(path), Text::copy(cache_path)) else {
        return 1;
    };
    let rows = if count == 0 {
        &[]
    } else {
        unsafe { slice::from_raw_parts(rows, count as usize) }
    };
    let diagnostics = xp_core::epub::diagnostic::SILENT;
    let mut writer = Writer::new(&path, &cache_path, resources, leaves);
    if writer.cache.begin_opf(&writer.streams.io()).is_err() {
        return 2;
    }
    for row in rows {
        let Some(href) = (unsafe { row.href.bytes() }) else {
            return 3;
        };
        if writer
            .cache
            .create_spine(&writer.streams.io(), href, &diagnostics)
            .is_err()
        {
            return 3;
        }
    }
    if writer.cache.end_opf(&writer.streams.io()).is_err() {
        return 4;
    }
    if writer
        .cache
        .begin_toc(&writer.streams.io(), &diagnostics)
        .is_err()
    {
        return 5;
    }
    for row in rows.iter().filter(|row| row.toc != 0) {
        let (Some(href), Some(title), Some(anchor)) = (
            unsafe { row.href.bytes() },
            unsafe { row.title.bytes() },
            unsafe { row.anchor.bytes() },
        ) else {
            return 6;
        };
        if writer
            .cache
            .create_toc(
                &writer.streams.io(),
                title,
                href,
                anchor,
                row.level,
                &diagnostics,
            )
            .is_err()
        {
            return 6;
        }
    }
    if writer.cache.end_toc(&writer.streams.io()).is_err() {
        return 7;
    }
    let mut metadata = Metadata::default();
    for (field, bytes) in [
        (&mut metadata.title, b"A\xff\0Z".as_slice()),
        (&mut metadata.author, b"Author"),
        (&mut metadata.language, b"ko"),
        (&mut metadata.cover, b"OPS/cover.png"),
        (&mut metadata.text_reference, b"OPS/ch0000.xhtml"),
    ] {
        if field.try_reserve_exact(bytes.len()).is_err() {
            return 8;
        }
        field.extend_from_slice(bytes);
    }
    if writer.build(&metadata, &diagnostics).is_err() {
        return 9;
    }
    0
}
