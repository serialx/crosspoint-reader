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
