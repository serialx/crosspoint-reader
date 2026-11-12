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
    u8::from(crate::mock_file(file, 13, ptr::null(), bytes.cast_mut(), length) == length)
}
#[export_name = "xp_native_page_image_decode"]
unsafe extern "C" fn mock_decode(native: *mut c_void, image: *const Image, cache: *const u8) -> u8 {
    crate::mock_decode(native, image.cast(), cache)
}
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
    fs::write(
        PathBuf::from(env::var_os("OUT_DIR").unwrap()).join("images.rs"),
        source,
    )
    .unwrap();
}
