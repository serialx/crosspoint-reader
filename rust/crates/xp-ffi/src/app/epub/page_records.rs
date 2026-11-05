use super::app_abi::Text as XpAppText;
#[repr(C)]
#[derive(Clone, Copy)]
pub struct XpEpubPageInfo {
    pub visible_offset: u32,
    pub elements: u16,
    pub footnotes: u16,
    pub links: u16,
    pub images: u8,
    pub images_need_decode: u8,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XpEpubElement {
    pub x: i32,
    pub y: i32,
    pub words: u16,
    pub tag: u8,
    pub valid: u8,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XpEpubWord {
    pub text: XpAppText,
    pub x: i32,
    pub source_start: u32,
    pub source_end: u32,
    pub style: u8,
    pub character_spacing: i8,
}

#[cfg(feature = "app-test")]
#[repr(C)]
#[derive(Clone, Copy)]
pub struct XpEpubFootnote {
    pub text: XpAppText,
    pub href: XpAppText,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct XpEpubLink {
    pub href: XpAppText,
    pub x: i32,
    pub y: i32,
    pub width: i32,
    pub height: i32,
}

#[cfg(feature = "app-test")]
pub type XpEpubFootnoteSink =
    unsafe extern "C" fn(context: *mut core::ffi::c_void, value: *const XpEpubFootnote) -> u8;
#[cfg(feature = "app-test")]
pub type XpEpubLinkSink =
    unsafe extern "C" fn(context: *mut core::ffi::c_void, value: *const XpEpubLink) -> u8;
