use super::{
    abi::XpEpubLeaves,
    render_resources::{
        physical::{self, Op, Query},
        PhysicalResources, PrewarmStorage,
    },
    EpubSession, ResourceAccess,
};
use core::ptr;

impl PhysicalResources for XpEpubLeaves {
    unsafe fn prewarm_begin(&self, storage: *mut PrewarmStorage) -> bool {
        unsafe { (self.fonts_prewarm_begin)(storage) != 0 }
    }
    unsafe fn prewarm_finish(&self, storage: *mut PrewarmStorage) {
        unsafe { (self.fonts_prewarm_finish_scan)(storage) };
    }
    unsafe fn prewarm_drop(&self, storage: *mut PrewarmStorage) {
        unsafe { (self.fonts_prewarm_drop)(storage) };
    }
    fn detach_source(&self) {
        unsafe { (self.display_source)(ptr::null_mut()) };
    }
    fn clear_plan(&self) {
        unsafe { (self.render_plan)(ptr::null_mut(), ptr::null_mut(), 0, 0) };
    }
    fn clear_window(&self) {
        unsafe { (self.image_read_window)(ptr::null_mut(), 0) };
    }
    fn release_images(&self) {
        unsafe { (self.image_release_cache)() };
    }
}
impl physical::Physical for XpEpubLeaves {
    fn query(&self, query: Query) -> u32 {
        unsafe { (self.render_physical_query)(query as u8) }
    }
    fn operation(&self, op: &Op) -> u32 {
        unsafe { (self.render_physical_operation)(op) }
    }
    #[cfg(feature = "reader-strips")]
    fn plan(
        &self,
        plan: *mut crate::page_render::Plan,
        bounds: *mut u8,
        count: u16,
        recording: bool,
    ) {
        unsafe { (self.render_plan)(plan, bounds, count, u8::from(recording)) };
    }
    #[cfg(feature = "reader-strips")]
    fn image_window(&self, bytes: *mut u8, length: u32) {
        unsafe { (self.image_read_window)(bytes, length) };
    }
    #[cfg(feature = "reader-strips")]
    fn source(&self, source: *mut crate::display_source::DisplaySource) -> bool {
        unsafe { (self.display_source)(source) != 0 }
    }
    #[cfg(feature = "reader-strips")]
    fn framebuffer(&self) -> (*mut u8, usize) {
        let mut size = 0;
        let bytes = unsafe { (self.framebuffer_view)(&mut size) };
        (bytes, size as usize)
    }
}
impl EpubSession {
    pub(super) fn release_idle_strips(&self, access: &ResourceAccess<'_>) -> bool {
        self.check_resources(access);
        #[cfg(feature = "app-test")]
        if let Some(fixture) = self.legacy_pages.get() {
            return unsafe { (fixture.strip_release_idle)(self.native(access)) != 0 };
        }
        #[cfg(feature = "reader-strips")]
        return self.page_resources(access).release_idle_strips();
        #[cfg(not(feature = "reader-strips"))]
        false
    }

    pub(super) fn refresh_frequency(&self, access: &ResourceAccess<'_>) -> i32 {
        self.check_resources(access);
        #[cfg(feature = "app-test")]
        if self.legacy_pages.get().is_some() {
            return self.legacy_query(access, xp_ui::reader::render::Query::Frequency) as i32;
        }
        // Read at each use, just as the native settings getter did.
        crate::settings::xp_setting_refresh(self.settings(false).refresh_frequency)
    }
}

impl EpubSession {
    pub(super) fn physical_query(&self, access: &ResourceAccess<'_>, query: Query) -> u32 {
        use physical::Physical;
        self.check_resources(access);
        #[cfg(feature = "app-test")]
        if self.legacy_pages.get().is_some() {
            let old = match query {
                Query::FreeHeap => xp_ui::reader::render::Query::FreeHeap,
                Query::MaxBlock => xp_ui::reader::render::Query::MaxBlock,
                Query::IsBw => xp_ui::reader::render::Query::IsBw,
                _ => panic!("legacy physical query"),
            };
            return self.legacy_query(access, old);
        }
        self.leaves.query(query)
    }
    pub(super) fn render_is_a4(&self, access: &ResourceAccess<'_>) -> bool {
        self.check_resources(access);
        #[cfg(feature = "app-test")]
        if self.legacy_pages.get().is_some() {
            return self.legacy_query(access, xp_ui::reader::render::Query::IsA4) != 0;
        }
        self.overlay_geometry().a4 != 0
    }
    #[cfg(feature = "app-test")]
    fn legacy_query(
        &self,
        access: &ResourceAccess<'_>,
        query: xp_ui::reader::render::Query,
    ) -> u32 {
        let services = unsafe { &*(self.legacy_render().render_services)(self.native(access)) };
        unsafe { services.read.expect("legacy render query")(services.context, query as u8) }
    }
}

pub(super) enum IdlePrewarm<'a, 'b> {
    Reader(
        super::render_resources::PrewarmLease<'a>,
        core::marker::PhantomData<&'b ()>,
    ),
    #[cfg(feature = "app-test")]
    Legacy(&'a ResourceAccess<'b>),
}
impl IdlePrewarm<'_, '_> {
    pub(super) fn finish_scan(&self) {
        match self {
            Self::Reader(lease, _) => lease.finish_scan(),
            #[cfg(feature = "app-test")]
            Self::Legacy(access) => unsafe {
                (access.owner.legacy_render().fonts_prewarm_finish_scan)(
                    access.owner.native(access),
                )
            },
        }
    }
}
impl Drop for IdlePrewarm<'_, '_> {
    fn drop(&mut self) {
        #[cfg(feature = "app-test")]
        if let Self::Legacy(access) = self {
            unsafe {
                (access.owner.legacy_render().fonts_prewarm_drop)(access.owner.native(access))
            };
        }
    }
}
impl EpubSession {
    pub(super) fn idle_prewarm<'a, 'b>(
        &'a self,
        access: &'a ResourceAccess<'b>,
    ) -> Option<IdlePrewarm<'a, 'b>> {
        self.check_resources(access);
        #[cfg(feature = "app-test")]
        if let Some(fixture) = self.legacy_pages.get() {
            return (unsafe { (fixture.fonts_prewarm_begin)(self.native(access)) } != 0)
                .then_some(IdlePrewarm::Legacy(access));
        }
        self.page_resources(access)
            .begin_idle_prewarm(self.leaves)
            .map(|lease| IdlePrewarm::Reader(lease, core::marker::PhantomData))
    }
}
