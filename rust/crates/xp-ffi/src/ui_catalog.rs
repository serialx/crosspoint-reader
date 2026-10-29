use xp_ui::catalog::{
    ffi::{self, Services},
    hits::Hits,
    state::State,
    view::Layout,
};
use xp_ui::{
    ffi::{Buffer, SceneView},
    model::{Effect, Input, Interest, ListState},
    scene::Interaction,
};
macro_rules! export {
 ($name:ident($($arg:ident:$ty:ty),*) -> $result:ty = $call:ident) => {
  /// # Safety
  /// Descriptor pointers follow the private catalog-family ABI borrowing contract.
  #[no_mangle]
  #[inline(never)]
  pub unsafe extern "C" fn $name($($arg:$ty),*) -> $result { unsafe { ffi::$call($($arg),*) } }
 };
}
export!(xp_ui_catalog_init(s:*mut State,k:u8,i:i32,p:u8)->()=init);
export!(xp_ui_catalog_interest(s:*const State,t:u32)->Interest=interest);
export!(xp_ui_catalog_update(s:*mut State,i:*const Input,n:u16,q:u8,p:u8)->Effect=update);
export!(xp_ui_catalog_publish(s:*mut State,n:*const ListState,v:i32)->()=publish);
export!(xp_ui_catalog_hit(h:*const Hits,x:i16,y:i16,o:*mut Interaction)->u8=hit);
export!(xp_ui_catalog_view(s:*const State,l:*const Layout,r:*const Services,b:*const Buffer,n:*mut ListState,o:*mut SceneView,h:*mut Hits)->u8=view);
export!(xp_ui_catalog_select(s:*mut State,i:i32,r:u8)->()=select);
export!(xp_ui_catalog_mode(s:*mut State,m:u8)->()=mode);
export!(xp_ui_catalog_awake(s:*const State)->u8=awake);
export!(xp_ui_catalog_progress(s:*mut State,d:u32,t:u32,n:u32)->u8=progress);

#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_catalog_window_take(
    s: *mut xp_ui::catalog::state::Window,
    eligible: u8,
) -> u8 {
    unsafe { s.as_mut() }.map_or(0, |s| u8::from(s.take(eligible != 0)))
}
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_catalog_page_template(s: *const u8, len: usize) -> u8 {
    if s.is_null() || len > isize::MAX as usize {
        return 0;
    }
    u8::from(xp_ui::catalog::state::page_template(unsafe {
        core::slice::from_raw_parts(s, len)
    }))
}
#[no_mangle]
#[inline(never)]
pub unsafe extern "C" fn xp_ui_catalog_item(
    s: *mut xp_ui::catalog::state::State,
    row: i32,
    count: i32,
) -> i32 {
    unsafe { s.as_mut() }.map_or(-2, |s| s.item(row, count))
}
