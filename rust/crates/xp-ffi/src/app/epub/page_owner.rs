use super::{abi::XpEpubLeaves, app_abi, book_owner::FileSlot};
use core::{alloc::Layout, ptr::NonNull, slice};
use xp_core::{page, section::Diagnostic};

mod diagnostics;
mod metadata;
mod transfer;
pub(super) use metadata::Metadata;
pub(super) use transfer::Pending;
#[cfg(feature = "app-test")]
mod tests;

#[repr(C)]
struct Header {
    resources: &'static app_abi::Resources,
    arena_len: usize,
}

#[cfg(target_pointer_width = "32")]
const _: () = assert!(core::mem::size_of::<Header>() == 8);

// Only the handle moves. Native loans point into its stable allocation.
pub(super) struct Page {
    allocation: NonNull<Header>,
}

impl Page {
    pub(super) fn borrow(&self) -> super::page_view::View<'_> {
        super::page_view::View::Arena(self.arena())
    }

    fn allocate(resources: &'static app_abi::Resources, bytes: usize) -> Option<Self> {
        let (layout, offset) = Layout::new::<Header>()
            .extend(Layout::array::<u8>(bytes).ok()?)
            .ok()?;
        debug_assert_eq!(offset, core::mem::size_of::<Header>());
        let allocation = NonNull::new(unsafe {
            (resources.buffer_allocate)(app_abi::MEMORY_DEFAULT, layout.size())
        })?
        .cast::<Header>();
        unsafe {
            allocation.as_ptr().write(Header {
                resources,
                arena_len: bytes,
            });
            // Rust slices require initialized bytes, including before decode.
            allocation
                .as_ptr()
                .add(1)
                .cast::<u8>()
                .write_bytes(0, bytes);
        }
        Some(Self { allocation })
    }

    pub(super) fn arena(&self) -> &[u8] {
        unsafe {
            slice::from_raw_parts(
                self.allocation.as_ptr().add(1).cast(),
                self.allocation.as_ref().arena_len,
            )
        }
    }

    fn arena_mut(&mut self) -> &mut [u8] {
        unsafe {
            slice::from_raw_parts_mut(
                self.allocation.as_ptr().add(1).cast(),
                self.allocation.as_ref().arena_len,
            )
        }
    }

    pub(super) fn view(&self) -> page::Page<'_> {
        page::Page::open(self.arena()).expect("validated immutable Page")
    }

    // Match native resolveViews admission without retaining decoded views.
    fn validate(&self) -> page::Result<()> {
        let page = page::Page::open(self.arena())?;
        for index in 0..page.element_count() {
            let (_, _, tag) = page.position(index)?;
            let element = page.element(index)?;
            match tag {
                1 => {
                    let line = element.line()?;
                    line.fonts()?;
                    line.block_style()?;
                    line.packed_data()?;
                    line.font_data()?;
                    line.ruby_offsets()?;
                    line.style_data()?;
                }
                2 => {
                    element.record()?;
                    element.image()?;
                }
                3 | 5 => {
                    element.record()?;
                }
                4 => {
                    element.record()?;
                    element.drop_cap_text()?;
                }
                _ => return Err(page::Error::Invalid),
            }
        }
        Ok(())
    }

    pub(super) fn load(
        resources: &'static app_abi::Resources,
        leaves: &XpEpubLeaves,
        file: &mut FileSlot,
        bytes: u32,
        visible: u32,
    ) -> Option<Self> {
        let start = file.physical_position();
        if bytes < 6 || start > file.size() || bytes > file.size() - start {
            return None;
        }
        let mut detail = Diagnostic::default();
        let sized = page::probe::probe_detailed(
            bytes as usize,
            |offset, output| {
                file.seek(start + offset as u32)
                    && unsafe { file.read(output.as_mut_ptr(), output.len()) }
                        == output.len() as i32
            },
            &mut detail,
        );
        let sized = match sized {
            Ok(sized) => sized,
            Err(_) => {
                diagnostics::decode(leaves, detail);
                return None;
            }
        };
        let Some(mut owner) = Self::allocate(resources, sized.arena_size) else {
            diagnostics::allocation(leaves);
            return None;
        };
        let offset = sized.arena_size - sized.consumed;
        let arena = owner.arena_mut();
        let loaded = file.seek(start)
            && unsafe { file.read(arena[offset..].as_mut_ptr(), sized.consumed) }
                == sized.consumed as i32;
        let decoded = loaded
            && page::decode_in_place_detailed(arena, offset, sized.consumed, visible, &mut detail)
                .is_ok_and(|info| info.elements == sized.elements)
            && owner.validate().is_ok();
        if !decoded {
            diagnostics::decode(leaves, detail);
            return None;
        }
        Some(owner)
    }

    #[cfg(feature = "app-test")]
    fn from_bytes(
        resources: &'static app_abi::Resources,
        leaves: &XpEpubLeaves,
        bytes: &[u8],
        visible: u32,
    ) -> Option<Self> {
        let mut detail = Diagnostic::default();
        let sized = match page::inspect_detailed(bytes, &mut detail) {
            Ok(sized) => sized,
            Err(_) => {
                diagnostics::decode(leaves, detail);
                return None;
            }
        };
        let Some(mut owner) = Self::allocate(resources, sized.arena_size) else {
            diagnostics::allocation(leaves);
            return None;
        };
        if page::decode_detailed(bytes, visible, owner.arena_mut(), &mut detail).is_err()
            || owner.validate().is_err()
        {
            diagnostics::decode(leaves, detail);
            return None;
        }
        Some(owner)
    }
}

impl Drop for Page {
    fn drop(&mut self) {
        let resources = unsafe { self.allocation.as_ref().resources };
        unsafe {
            (resources.buffer_release)(app_abi::MEMORY_DEFAULT, self.allocation.as_ptr().cast());
        }
    }
}
