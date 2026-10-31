//! Borrowed mirrors of EpdFontData.h; layout checked on host and C3.

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct Glyph {
    pub width: u8,
    pub height: u8,
    pub advance_x: u16,
    pub left: i16,
    pub top: i16,
    pub data_length: u16,
    pub data_offset: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct Group {
    pub compressed_offset: u32,
    pub compressed_size: u32,
    pub uncompressed_size: u32,
    pub glyph_count: u16,
    pub first_glyph_index: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct Interval {
    pub first: u32,
    pub last: u32,
    pub offset: u32,
}

#[repr(C, packed)]
#[derive(Clone, Copy, Debug)]
pub struct KernClass {
    pub codepoint: u16,
    pub class_id: u8,
}

#[repr(C, packed)]
#[derive(Clone, Copy, Debug)]
pub struct Ligature {
    pub pair: u32,
    pub ligature_cp: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct FontData {
    pub bitmap: *const u8,
    pub glyph: *const Glyph,
    pub intervals: *const Interval,
    pub interval_count: u32,
    pub advance_y: u8,
    pub ascender: i32,
    pub descender: i32,
    pub is2_bit: bool,
    pub groups: *const Group,
    pub group_count: u16,
    pub glyph_to_group: *const u16,
    pub kern_left_classes: *const KernClass,
    pub kern_right_classes: *const KernClass,
    pub kern_left_codepoints: *const u16,
    pub kern_left_class_ids: *const u8,
    pub kern_right_codepoints: *const u16,
    pub kern_right_class_ids: *const u8,
    pub kern_matrix: *const i8,
    pub kern_row_offsets: *const u16,
    pub kern_sparse_cols: *const u8,
    pub kern_sparse_values: *const i8,
    pub kern_left_entry_count: u16,
    pub kern_right_entry_count: u16,
    pub kern_left_class_count: u8,
    pub kern_right_class_count: u8,
    pub ligature_pairs: *const Ligature,
    pub ligature_pair_count: u32,
    pub glyph_miss_handler:
        Option<unsafe extern "C" fn(*mut core::ffi::c_void, u32) -> *const Glyph>,
    pub glyph_miss_ctx: *mut core::ffi::c_void,
    pub coverage_handler: Option<unsafe extern "C" fn(*mut core::ffi::c_void, u32) -> bool>,
    pub vector_bitmap_handler:
        Option<unsafe extern "C" fn(*mut core::ffi::c_void, *const Glyph) -> *const u8>,
    pub kern_handler: Option<unsafe extern "C" fn(*mut core::ffi::c_void, u32, u32) -> i8>,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct Advance {
    pub codepoint: u32,
    pub advance_x: u16,
}

#[repr(C, packed)]
#[derive(Clone, Copy, Debug)]
pub struct BmpInterval {
    pub first: u16,
    pub last: u16,
    pub offset: u16,
}
