use super::{EpubSession, ResourceAccess};

impl EpubSession {
    pub(super) fn link_at_point(
        &self,
        access: &ResourceAccess<'_>,
        x: i32,
        y: i32,
        left: i32,
        top: i32,
    ) -> Option<u16> {
        for index in 0..self.metadata_links(access) {
            let Some(link) = self.metadata_link(access, index) else {
                continue;
            };
            let link = xp_core::page::Link {
                href: &[],
                x: link.x as i16,
                y: link.y as i16,
                width: link.width as i16,
                height: link.height as i16,
            };
            if link.contains(x.wrapping_sub(left), y.wrapping_sub(top), 6, 28) {
                return Some(index);
            }
        }
        None
    }
}
