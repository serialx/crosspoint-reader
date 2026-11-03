use super::{app_abi, state::Position, EpubSession, ResourceAccess, Text};
use core::{ffi::c_void, ptr::NonNull};
use xp_ui::reader::{links_decode, links_encode, LinkPosition};

pub(super) enum FileMode {
    Read,
    Write,
}

pub(super) struct File<'a> {
    handle: NonNull<c_void>,
    access: &'a ResourceAccess<'a>,
}

impl File<'_> {
    pub(super) fn read(&self, bytes: &mut [u8]) -> i32 {
        unsafe {
            (self.access.owner.leaves.file_read)(
                self.handle.as_ptr(),
                bytes.as_mut_ptr(),
                bytes.len() as u32,
            )
        }
    }

    fn write(&self, bytes: &[u8]) -> u32 {
        unsafe {
            (self.access.owner.leaves.file_write)(
                self.handle.as_ptr(),
                bytes.as_ptr(),
                bytes.len() as u32,
            )
        }
    }
}

impl Drop for File<'_> {
    fn drop(&mut self) {
        unsafe { (self.access.owner.leaves.file_close)(self.handle.as_ptr()) };
    }
}

impl EpubSession {
    pub(super) fn open_file<'a>(
        &self,
        access: &'a ResourceAccess<'a>,
        path: &Text,
        mode: FileMode,
    ) -> Option<File<'a>> {
        assert!(core::ptr::eq(self, access.owner));
        let handle = unsafe {
            match mode {
                FileMode::Read => (self.leaves.file_open_read)(
                    path.wire(),
                    app_abi::Text::from_bytes(c"ERS".to_bytes()),
                ),
                FileMode::Write => (self.leaves.file_open_write)(
                    path.wire(),
                    app_abi::Text::from_bytes(c"ERS".to_bytes()),
                ),
            }
        };
        Some(File {
            handle: NonNull::new(handle)?,
            access,
        })
    }

    // One fallible path allocation, replacing the native concatenated string.
    pub(super) fn cache_path(&self, access: &ResourceAccess<'_>, suffix: &[u8]) -> Option<Text> {
        self.book_cache_path(self.book(access)?, suffix)
    }

    pub(super) fn book_cache_path(&self, book: NonNull<c_void>, suffix: &[u8]) -> Option<Text> {
        struct Capture<'a> {
            suffix: &'a [u8],
            result: Option<Text>,
        }
        unsafe extern "C" fn receive(context: *mut c_void, text: app_abi::Text) -> u8 {
            let capture = &mut *context.cast::<Capture<'_>>();
            let Some(bytes) = text.bytes() else { return 0 };
            let mut path = Text::default();
            if path.assign_parts(&[bytes, capture.suffix]).is_err() {
                return 0;
            }
            capture.result = Some(path);
            1
        }
        let mut capture = Capture {
            suffix,
            result: None,
        };
        if unsafe {
            (super::session_book::cache_path)(
                book.as_ptr(),
                receive,
                (&mut capture as *mut Capture<'_>).cast(),
            )
        } == 0
        {
            return None;
        }
        capture.result
    }

    pub(super) fn save_links(&self, access: &ResourceAccess<'_>) {
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.lifetime_overrides {
            unsafe { (hook.save_links)(hook.context) };
            return;
        }
        let (positions, depth) = self.model(|state| {
            (
                state.history.positions.map(|p| LinkPosition {
                    spine: p.spine,
                    page: p.page,
                }),
                state.history.depth,
            )
        });
        let mut bytes = [0; 13];
        let count = links_encode(&positions, depth, &mut bytes);
        let Some(path) = self.cache_path(access, b"/links.bin") else {
            return;
        };
        let Some(file) = self.open_file(access, &path, FileMode::Write) else {
            return;
        };
        if file.write(&bytes[..count as usize]) != count {
            unsafe {
                (self.leaves.log)(
                    1,
                    app_abi::Text::from_bytes(b"ERS"),
                    app_abi::Text::from_bytes(b"Failed to write link stack"),
                )
            };
        }
    }

    pub(super) fn load_links(&self, access: &ResourceAccess<'_>) {
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.lifetime_overrides {
            unsafe { (hook.load_links)(hook.context) };
            return;
        }
        let Some(path) = self.cache_path(access, b"/links.bin") else {
            return;
        };
        if unsafe { (self.resources.path_exists)(path.wire()) } == 0 {
            return;
        }
        if let Some(file) = self.open_file(access, &path, FileMode::Read) {
            let mut bytes = [0; 13];
            let size = file.read(&mut bytes);
            let mut positions = self.model(|state| {
                state.history.positions.map(|p| LinkPosition {
                    spine: p.spine,
                    page: p.page,
                })
            });
            let book = self.book(access).expect("link book");
            let count = unsafe { (super::session_book::spine_count)(book.as_ptr()) };
            let depth = if (0..=13).contains(&size) {
                links_decode(&bytes[..size as usize], count, &mut positions)
            } else {
                0
            };
            self.model(|state| {
                state.history.positions = positions.map(|p| Position {
                    spine: p.spine,
                    page: p.page,
                });
                if depth != 0 {
                    state.history.depth = depth;
                }
            });
            if depth != 0 {
                self.loop_log(format_args!("Loaded link stack, depth {depth}"), &[]);
            }
        }
        unsafe { (self.leaves.path_remove)(path.wire()) };
    }
}
