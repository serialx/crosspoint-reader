use std::{env, fs, path::PathBuf};

fn main() {
    let path = PathBuf::from("../../rust/crates/xp-ffi/src/page_render/images.rs");
    println!("cargo:rerun-if-changed={}", path.display());
    // The source is compiled verbatim apart from its module-doc marker.
    let mut source = fs::read_to_string(path).unwrap().replacen("//!", "//", 1);
    source.push_str(r#"
#[export_name = "xp_native_page_image_file"]
unsafe extern "C" fn mock_file(file: *mut FileStorage, op: FileOp, path: *const u8, bytes: *mut u8, length: usize) -> usize {
    crate::mock_file(file.cast(), op as u32, path, bytes, length)
}
#[export_name = "xp_native_page_image_write"]
unsafe extern "C" fn mock_write(file: *mut c_void, bytes: *const u8, length: usize) -> u8 {
    u8::from(crate::mock_file(file, 1000, ptr::null(), bytes.cast_mut(), length) == length)
}
#[export_name = "xp_native_page_image_codec_log"]
unsafe extern "C" fn mock_codec_log(_: *const c_void, _: u32, _: u32, _: u32, _: u32, _: u32) {}
#[export_name = "xp_native_page_image_log"]
unsafe extern "C" fn mock_log(native: *mut c_void, image: *const Image, cache: *const u8, event: u32, a: i32, b: i32) {
    crate::mock_log(native, image.cast(), cache, event, a, b)
}
#[cfg(test)]
#[test]
fn native_file_storage_is_not_unpin() {
    trait Ambiguous<A> { fn marker() {} }
    impl<T: ?Sized> Ambiguous<()> for T {}
    struct IfUnpin;
    impl<T: ?Sized + Unpin> Ambiguous<IfUnpin> for T {}
    let _ = <File as Ambiguous<_>>::marker;
}
"#);
    let decode = PathBuf::from("../../rust/crates/xp-ffi/src/page_render/images/decode.rs");
    println!("cargo:rerun-if-changed={}", decode.display());
    let directory = PathBuf::from(env::var_os("OUT_DIR").unwrap()).join("images");
    fs::create_dir_all(&directory).unwrap();
    fs::copy(decode, directory.join("decode.rs")).unwrap();
    let jpeg = PathBuf::from("../../rust/crates/xp-ffi/src/page_render/images/jpeg.rs");
    println!("cargo:rerun-if-changed={}", jpeg.display());
    let mut jpeg_source = fs::read_to_string(jpeg).unwrap();
    jpeg_source.push_str(r#"
#[export_name = "xp_native_page_jpeg"]
unsafe extern "C" fn mock_jpeg(raw: *mut Native, op: Operation, _: u32) -> u32 {
    // Keep the native record loan live while the callback mutates disjoint Rust rows.
    let state = &mut *raw;
    crate::EVENTS.with(|events| events.borrow_mut().push(2000 + core::ptr::from_ref(&op).cast::<u32>().read()));
    match op {
        Operation::Allocate => { state.decoder = core::ptr::NonNull::<u8>::dangling().as_ptr().cast(); 1 }
        Operation::Open => 1,
        Operation::Facts => { state.width = 256; state.height = 256; 1 }
        Operation::Configure => 1,
        Operation::Decode => {
            let pixels = [128u8; 512];
            #[repr(C)]
            struct MockRow { x: i32, y: i32, width: i32, height: i32, stride: i32, pixels: *const u8, pixel_count: usize, palette: *const u8, palette_count: usize, transparent: u32, format: u8, depth: u8, alpha: u8 }
            let mut row = MockRow { x: 0, y: 0, width: 32, height: 16, stride: 32,
                pixels: pixels.as_ptr(), pixel_count: 512, palette: core::ptr::null(),
                palette_count: 0, transparent: 0, format: 0, depth: 8, alpha: 0 };
            assert_eq!(xp_page_image_jpeg_row(state.rows, core::ptr::from_ref(&row).cast()), 1);
            row.y = 16;
            let result = xp_page_image_jpeg_row(state.rows, core::ptr::from_ref(&row).cast());
            state.result = if crate::FAULT.with(core::cell::Cell::get) == 2004 { 0 } else { result };
            state.error = 0;
            state.result as u32
        }
        Operation::Close => 1,
        Operation::Free => { state.decoder = core::ptr::null_mut(); 1 }
    }
}
#[export_name = "xp_native_page_jpeg_log"]
unsafe extern "C" fn mock_jpeg_log(_: *const Native, _: u32, _: u32) {}
"#);
    fs::write(directory.join("jpeg.rs"), jpeg_source).unwrap();
    let book = PathBuf::from("../../rust/crates/xp-ffi/src/book_parser.rs");
    println!("cargo:rerun-if-changed={}", book.display());
    let book = fs::read_to_string(book).unwrap();
    let services = &book
        [book.find("use core::ffi::c_void;").unwrap()..book.find("struct ParseServices").unwrap()];
    let start = book.find("enum JpegOp").unwrap();
    let jpeg = &book[book[..start].rfind("#[repr(u32)]").unwrap()..];
    fs::write(
        directory.parent().unwrap().join("book_parser.rs"),
        format!("{services}\n{jpeg}"),
    )
    .unwrap();

    fs::write(
        PathBuf::from(env::var_os("OUT_DIR").unwrap()).join("images.rs"),
        source,
    )
    .unwrap();
}
