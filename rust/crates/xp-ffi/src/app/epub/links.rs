use super::{abi, EpubSession, ResourceAccess};
use core::ffi::c_void;

impl EpubSession {
    pub(super) fn link_at_point(
        &self,
        access: &ResourceAccess<'_>,
        x: i32,
        y: i32,
        left: i32,
        top: i32,
    ) -> Option<u16> {
        struct Hit {
            x: i32,
            y: i32,
            found: bool,
        }
        unsafe extern "C" fn test(context: *mut c_void, link: *const abi::XpEpubLink) -> u8 {
            let hit = &mut *context.cast::<Hit>();
            let link = &*link;
            let bounds = [
                link.x as i16,
                link.y as i16,
                link.width as i16,
                link.height as i16,
            ];
            hit.found =
                crate::book_policy::xp_book_link_contains(&bounds, hit.x, hit.y, 6, 28) != 0;
            1
        }
        let native = self.native(access);
        let length = unsafe { (self.leaves.metadata_links)(native) };
        let mut hit = Hit {
            x: x.wrapping_sub(left),
            y: y.wrapping_sub(top),
            found: false,
        };
        for index in 0..length {
            unsafe {
                (self.leaves.metadata_link)(native, index, test, (&mut hit as *mut Hit).cast())
            };
            if hit.found {
                return Some(index);
            }
        }
        None
    }
}
