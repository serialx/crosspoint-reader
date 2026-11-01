use super::{app_abi, EpubSession, Text};
use core::ffi::c_void;

pub(super) fn is_in_read_folder(path: &[u8]) -> bool {
    path.starts_with(b"/read/")
}

fn decimal(mut value: u64, bytes: &mut [u8; 20]) -> &[u8] {
    let mut start = bytes.len();
    loop {
        start -= 1;
        bytes[start] = b'0' + (value % 10) as u8;
        value /= 10;
        if value == 0 {
            return &bytes[start..];
        }
    }
}

impl EpubSession {
    pub(super) fn read_folder_destination(&self, source: &Text) -> Option<Text> {
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.lifetime_overrides {
            return Text::receive(|sink, context| unsafe {
                (hook.destination)(hook.context, source.wire(), sink, context)
            });
        }
        let filename = source
            .bytes()
            .rsplit(|&byte| byte == b'/')
            .next()
            .unwrap_or_default();
        unsafe { (self.leaves.path_mkdir)(app_abi::Text::from_bytes(&b"/read\0"[..5])) };
        let mut destination = Text::default();
        destination.assign_parts(&[b"/read/", filename]).ok()?;
        if unsafe { (self.resources.path_exists)(destination.wire()) } == 0 {
            return Some(destination);
        }
        let dot = filename
            .iter()
            .rposition(|&byte| byte == b'.')
            .unwrap_or(filename.len());
        let (base, extension) = filename.split_at(dot);
        let mut number = [0; 20];
        for suffix in 2..=99 {
            destination
                .assign_parts(&[
                    b"/read/",
                    base,
                    b" (",
                    decimal(suffix, &mut number),
                    b")",
                    extension,
                ])
                .ok()?;
            let exists = unsafe { (self.resources.path_exists)(destination.wire()) } != 0;
            if !exists || suffix == 99 {
                break;
            }
        }
        Some(destination)
    }

    pub(super) fn move_to_read_folder(&self, source: &Text, destination: &Text, old_cache: &Text) {
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.lifetime_overrides {
            unsafe {
                (hook.move_book)(
                    hook.context,
                    source.wire(),
                    destination.wire(),
                    old_cache.wire(),
                )
            };
            return;
        }
        // Reserve the largest scratch paths before moving anything. Reusing them
        // keeps allocation failure out of the sidecar rollback sequence.
        let mut from = Text::default();
        let mut to = Text::default();
        let mut cache = Text::default();
        if from.assign_parts(&[source.bytes(), b".rights"]).is_err()
            || to.assign_parts(&[destination.bytes(), b".rights"]).is_err()
            || cache
                .assign_parts(&[b"/.crosspoint/epub_", b"18446744073709551615"])
                .is_err()
        {
            return;
        }
        self.loop_log_parts(
            2,
            &[
                b"Moving finished epub: ",
                source.bytes(),
                b" -> ",
                destination.bytes(),
            ],
        );
        if unsafe { (self.leaves.clippings_move_book)(source.wire(), destination.wire()) } == 0 {
            self.loop_log_parts(
                1,
                &[b"Failed to move finished book and clippings to '/Read' folder"],
            );
            return;
        }
        const SIDECARS: [&[u8]; 2] = [b".key", b".rights"];
        for (index, sidecar) in SIDECARS.iter().enumerate() {
            from.assign_parts(&[source.bytes(), sidecar])
                .expect("reserved source sidecar");
            if unsafe { (self.resources.path_exists)(from.wire()) } == 0 {
                continue;
            }
            to.assign_parts(&[destination.bytes(), sidecar])
                .expect("reserved destination sidecar");
            if unsafe { (self.leaves.path_rename)(from.wire(), to.wire()) } != 0 {
                continue;
            }
            self.loop_log_parts(
                1,
                &[
                    b"Failed to move sidecar ",
                    from.bytes(),
                    b" -> ",
                    to.bytes(),
                ],
            );
            for prior in &SIDECARS[..index] {
                from.assign_parts(&[source.bytes(), prior])
                    .expect("reserved source rollback");
                to.assign_parts(&[destination.bytes(), prior])
                    .expect("reserved destination rollback");
                unsafe { (self.leaves.path_rename)(to.wire(), from.wire()) };
            }
            if unsafe { (self.leaves.path_rename)(destination.wire(), source.wire()) } == 0 {
                self.loop_log_parts(
                    1,
                    &[
                        b"Failed to restore epub after sidecar move failure: ",
                        destination.bytes(),
                        b" -> ",
                        source.bytes(),
                    ],
                );
            }
            return;
        }
        let hash = unsafe { (self.leaves.native_path_hash)(destination.wire()) };
        let mut number = [0; 20];
        cache
            .assign_parts(&[b"/.crosspoint/epub_", decimal(hash, &mut number)])
            .expect("reserved cache path");
        let mut moved = false;
        if !old_cache.is_empty() && unsafe { (self.resources.path_exists)(old_cache.wire()) } != 0 {
            moved = unsafe { (self.leaves.path_rename)(old_cache.wire(), cache.wire()) } != 0;
            if !moved {
                self.loop_log_parts(
                    1,
                    &[
                        b"Failed to rename cache dir ",
                        old_cache.bytes(),
                        b" -> ",
                        cache.bytes(),
                        b" (non-fatal)",
                    ],
                );
            }
        }
        unsafe {
            (self.leaves.recents_update_path)(
                source.wire(),
                destination.wire(),
                old_cache.wire(),
                if moved {
                    cache.wire()
                } else {
                    old_cache.wire()
                },
            )
        };
        struct Compare<'a> {
            source: &'a [u8],
            matches: bool,
        }
        unsafe extern "C" fn compare(context: *mut c_void, path: app_abi::Text) -> u8 {
            let compare = &mut *context.cast::<Compare<'_>>();
            compare.matches = path.bytes() == Some(compare.source);
            1
        }
        let mut current = Compare {
            source: source.bytes(),
            matches: false,
        };
        unsafe {
            (self.leaves.state_open_path)(compare, (&mut current as *mut Compare<'_>).cast())
        };
        if current.matches {
            unsafe {
                (self.leaves.state_set_open_path)(destination.wire());
                (self.leaves.state_save)();
            }
        }
    }
}
