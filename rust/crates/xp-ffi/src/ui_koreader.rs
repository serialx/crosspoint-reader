use core::ffi::c_char;
use xp_ui::{
    ffi::{Buffer, SceneView},
    koreader::{abi, Layout, Options, Services, State},
    model::{Effect, Input, Interest},
};
macro_rules! export {
    ($name:ident($($arg:ident:$ty:ty),*) -> $result:ty = $call:ident) => {
        /// # Safety
        /// Descriptors follow the private KOReader-family ABI borrowing contract.
        #[no_mangle]
        #[inline(never)]
        pub unsafe extern "C" fn $name($($arg:$ty),*) -> $result {
            unsafe { abi::$call($($arg),*) }
        }
    };
}
export!(xp_ui_koreader_init(s:*mut State,k:u8,a:u8,p:u8)->()=init);
export!(xp_ui_koreader_publish(s:*mut State,p:u8,i:u8)->()=publish);
export!(xp_ui_koreader_mark_return(s:*mut State,t:u32)->()=mark_return);
export!(xp_ui_koreader_busy(s:*const State)->u8=busy);
export!(xp_ui_koreader_interest(s:*const State,t:u32)->Interest=interest);
export!(xp_ui_koreader_update(s:*mut State,i:*const Input)->Effect=update);
export!(xp_ui_koreader_sync(o:*const Options,s:*const Services)->()=sync);
export!(xp_ui_koreader_upload(o:*const Options,s:*const Services)->()=upload);
export!(xp_ui_koreader_auth(k:u8,s:*const Services)->()=auth);
export!(xp_ui_koreader_save(s:*const Services)->()=save);
export!(xp_ui_koreader_view(s:*const State,l:*const Layout,b:*const Buffer,o:*mut SceneView)->u8=view);
export!(xp_ui_koreader_url_empty(p:*const u8,n:u32)->u8=url_empty);
#[no_mangle]
#[inline(never)]
pub extern "C" fn xp_ui_koreader_text(language: u8, handle: u32) -> *const c_char {
    abi::text(language, handle)
}
#[no_mangle]
#[inline(never)]
pub extern "C" fn xp_ui_koreader_edit(row: u8, value: u8, credentials: u8) -> Effect {
    abi::edit(row, value, credentials)
}
#[no_mangle]
#[inline(never)]
pub extern "C" fn xp_ui_koreader_label(row: u8) -> u16 {
    abi::label(row)
}
