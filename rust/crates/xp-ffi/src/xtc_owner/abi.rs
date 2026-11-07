use core::ffi::c_void;
use xp_core::book_io::xtc_reader::State;

pub const OWNER_WORDS: usize = if core::mem::size_of::<usize>() == 4 {
    40
} else {
    26
};
#[repr(C)]
pub struct XpXtcOwnerStorage {
    pub words: [usize; OWNER_WORDS],
}
#[repr(C)]
pub struct XpXtcChapterRow {
    pub name: *const u8,
    pub name_len: usize,
    pub inline_name: [u8; 16],
    pub start_page: u16,
    pub end_page: u16,
}
#[repr(C)]
pub struct XpXtcView {
    pub state: *const State,
    pub title: *const u8,
    pub title_len: usize,
    pub author: *const u8,
    pub author_len: usize,
    pub chapters: *const XpXtcChapterRow,
    pub chapter_count: usize,
}
#[repr(C)]
pub struct XpXtcLeaves {
    pub open: unsafe extern "C" fn(*mut c_void, *const u8) -> bool,
    pub is_open: unsafe extern "C" fn(*mut c_void) -> bool,
    pub close: unsafe extern "C" fn(*mut c_void),
    pub read: unsafe extern "C" fn(*mut c_void, *mut u8, usize) -> i32,
    pub seek: unsafe extern "C" fn(*mut c_void, u64) -> bool,
    pub size: unsafe extern "C" fn(*mut c_void) -> u64,
    pub allocate: unsafe extern "C" fn(usize, bool) -> *mut u8,
    pub release: unsafe extern "C" fn(*mut u8, usize),
    pub diagnostic: unsafe extern "C" fn(*mut c_void, u32, u32, u32, u32, u32),
}
#[repr(C)]
pub struct XpXtcStream {
    pub context: *mut c_void,
    pub emit: unsafe extern "C" fn(*mut c_void, *const u8, usize, usize),
}
