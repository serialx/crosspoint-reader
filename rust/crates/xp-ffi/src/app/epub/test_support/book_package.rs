use super::super::{zip::Archive, Metadata, Text, XpEpubLeaves};
use super::*;
use crate::app::abi;
use core::{ffi::c_void, slice};
use xp_core::epub::{diagnostic::SILENT, opf};

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_package_diagnostic(
    leaves: &'static XpEpubLeaves,
    event: u32,
    a: u32,
    text: abi::Text,
    other: abi::Text,
) {
    let service = diagnostics::services(leaves);
    unsafe {
        service.diagnostic.unwrap()(
            service.context,
            event,
            a,
            0,
            0,
            0,
            text.data,
            text.len as u32,
            other.data,
            other.len as u32,
        );
    }
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_package_error(
    leaves: &'static XpEpubLeaves,
    kind: u32,
    code: u32,
    line: u32,
    buffer: u8,
    allocation: u8,
) {
    let kind = match kind {
        0 => Kind::Container,
        1 => Kind::Opf,
        2 => Kind::Ncx,
        3 => Kind::Nav,
        _ => Kind::Encryption,
    };
    if allocation != 0 {
        assert!(Document::new(leaves, kind, &[], 0, false, None).is_err());
    } else {
        diagnostics::error(leaves, kind, &Error { code, line, buffer });
    }
}

fn stream(
    archive: &mut Archive<'_>,
    name: &[u8],
    document: &mut Document<'_, '_>,
    chunk: usize,
) -> bool {
    unsafe extern "C" fn sink(raw: *mut c_void, bytes: *const u8, length: usize) -> u8 {
        let document = unsafe { &mut *raw.cast::<Document<'_, '_>>() };
        let bytes = if length == 0 {
            &[]
        } else {
            unsafe { slice::from_raw_parts(bytes, length) }
        };
        u8::from(document.write(bytes, &SILENT) == length)
    }
    unsafe {
        archive.stream(
            name,
            chunk,
            false,
            sink,
            (document as *mut Document<'_, '_>).cast(),
        )
    }
}

#[no_mangle]
pub unsafe extern "C" fn xp_epub_test_package(
    resources: &'static abi::Resources,
    leaves: &'static XpEpubLeaves,
    path: abi::Text,
    cache_path: abi::Text,
    chunk: usize,
    prefer_ncx: u8,
) -> u32 {
    let (Some(path), Some(cache_path)) = (unsafe { path.bytes() }, unsafe { cache_path.bytes() })
    else {
        return 2;
    };
    let (Ok(path), Ok(cache_path)) = (Text::copy(path), Text::copy(cache_path)) else {
        return 2;
    };
    let mut archive = Archive::new(&path, resources, leaves);
    let container = b"META-INF/container.xml";
    let Some(entry) = archive.lookup(container) else {
        return 3;
    };
    let Ok(mut document) = Document::new(
        leaves,
        Kind::Container,
        &[],
        entry.uncompressed_size as usize,
        false,
        None,
    ) else {
        return 3;
    };
    if !stream(&mut archive, container, &mut document, chunk) || document.value(0).is_empty() {
        return 3;
    }
    let Ok(opf_path) = Text::copy(document.value(0)) else {
        return 3;
    };
    drop(document);
    let base = &opf_path.bytes()[..opf_path
        .bytes()
        .iter()
        .rposition(|&byte| byte == b'/')
        .map_or(0, |index| index + 1)];
    let mut writer = Writer::new(&path, &cache_path, resources, leaves);
    if writer.cache.begin_opf(&writer.streams.io()).is_err() {
        return 4;
    }
    let Some(entry) = archive.lookup(opf_path.bytes()) else {
        return 5;
    };
    let Ok(mut document) = Document::new(
        leaves,
        Kind::Opf,
        base,
        entry.uncompressed_size as usize,
        false,
        Some(&mut writer),
    ) else {
        return 5;
    };
    if !stream(&mut archive, opf_path.bytes(), &mut document, chunk) {
        return 5;
    }
    let mut metadata = Metadata::default();
    for id in [
        opf::TITLE,
        opf::AUTHOR,
        opf::LANGUAGE,
        opf::COVER,
        opf::TEXT,
    ] {
        if metadata.assign(id, document.value(id)).is_err() {
            return 5;
        }
    }
    let (Ok(ncx), Ok(nav)) = (
        Text::copy(document.value(opf::NCX)),
        Text::copy(document.value(opf::NAV)),
    ) else {
        return 5;
    };
    drop(document);
    if writer.cache.end_opf(&writer.streams.io()).is_err()
        || writer
            .cache
            .begin_toc(&writer.streams.io(), &SILENT)
            .is_err()
    {
        return 5;
    }
    let selected = if !ncx.is_empty() && (prefer_ncx != 0 || nav.is_empty()) {
        Some((Kind::Ncx, ncx.bytes(), base))
    } else if !nav.is_empty() {
        let end = nav
            .bytes()
            .iter()
            .rposition(|&byte| byte == b'/')
            .map_or(0, |index| index + 1);
        Some((Kind::Nav, nav.bytes(), &nav.bytes()[..end]))
    } else {
        None
    };
    if let Some((kind, name, base)) = selected {
        let Some(entry) = archive.lookup(name) else {
            return 6;
        };
        let Ok(mut document) = Document::new(
            leaves,
            kind,
            base,
            entry.uncompressed_size as usize,
            false,
            Some(&mut writer),
        ) else {
            return 6;
        };
        if !stream(&mut archive, name, &mut document, chunk) {
            return 6;
        }
    }
    if writer.cache.end_toc(&writer.streams.io()).is_err() {
        return 7;
    }
    if writer.build(&metadata, &SILENT).is_err() {
        return 8;
    }
    let encryption = b"META-INF/encryption.xml";
    if archive.lookup(encryption).is_some() {
        let Ok(mut document) = Document::new(leaves, Kind::Encryption, &[], 0, false, None) else {
            return 9;
        };
        if !stream(&mut archive, encryption, &mut document, chunk) || !document.finish(&SILENT) {
            return 9;
        }
    }
    0
}
