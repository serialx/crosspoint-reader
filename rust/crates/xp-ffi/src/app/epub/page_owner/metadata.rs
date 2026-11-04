use super::{app_abi, Page, XpEpubLeaves};
use crate::app::{epub::book_owner::log, resources::Buffer};
use core::slice;
use xp_core::page::Link;

#[derive(Default)]
pub(in crate::app::epub) struct Metadata {
    footnotes: Option<Buffer>,
    links: Option<Buffer>,
}

impl Metadata {
    pub(in crate::app::epub) fn clear(&mut self) {
        self.footnotes = None;
        self.links = None;
    }

    pub(in crate::app::epub) fn assign(
        &mut self,
        resources: &'static app_abi::Resources,
        leaves: &XpEpubLeaves,
        page: &Page,
    ) -> bool {
        self.clear();
        let view = page.view();
        let notes = usize::from(view.footnote_count());
        let links = usize::from(view.link_count());
        if notes > 16 || links > 32 {
            return false;
        }
        let new_notes = Buffer::allocate(resources, app_abi::MEMORY_DEFAULT, notes * 288);
        if notes != 0 && new_notes.is_none() {
            log::message(
                leaves,
                1,
                b"PGE",
                format_args!("OOM: footnotes {} bytes", notes * 288),
            );
            return false;
        }
        let new_links = Buffer::allocate(resources, app_abi::MEMORY_DEFAULT, links * 264);
        if links != 0 && new_links.is_none() {
            log::message(
                leaves,
                1,
                b"PGE",
                format_args!("OOM: links {} bytes", links * 264),
            );
            return false;
        }
        if let Some(buffer) = &new_notes {
            for index in 0..notes {
                let Ok((number, href)) = view.footnote(index as u16) else {
                    return false;
                };
                unsafe {
                    let output = buffer.as_ptr().add(index * 288);
                    output.copy_from_nonoverlapping(number.as_ptr(), 32);
                    output.add(32).copy_from_nonoverlapping(href.as_ptr(), 256);
                }
            }
        }
        if let Some(buffer) = &new_links {
            for index in 0..links {
                let Ok(link) = view.link(index as u16) else {
                    return false;
                };
                unsafe {
                    let output = buffer.as_ptr().add(index * 264);
                    output.copy_from_nonoverlapping(link.href.as_ptr(), 256);
                    for (field, value) in [link.x, link.y, link.width, link.height]
                        .into_iter()
                        .enumerate()
                    {
                        output
                            .add(256 + field * 2)
                            .copy_from_nonoverlapping(value.to_le_bytes().as_ptr(), 2);
                    }
                }
            }
        }
        self.footnotes = new_notes;
        self.links = new_links;
        true
    }

    pub(in crate::app::epub) fn footnote_count(&self) -> u16 {
        self.footnotes
            .as_ref()
            .map_or(0, |v| (v.len() / 288) as u16)
    }

    pub(in crate::app::epub) fn link_count(&self) -> u16 {
        self.links.as_ref().map_or(0, |v| (v.len() / 264) as u16)
    }

    fn record(buffer: &Option<Buffer>, index: u16, size: usize) -> Option<&[u8]> {
        let buffer = buffer.as_ref()?;
        let start = usize::from(index) * size;
        if start + size > buffer.len() {
            return None;
        }
        // Assignment initializes every record before publishing either buffer.
        Some(unsafe { slice::from_raw_parts(buffer.as_ptr().add(start), size) })
    }

    pub(in crate::app::epub) fn footnote(&self, index: u16) -> Option<(&[u8], &[u8])> {
        Some(Self::record(&self.footnotes, index, 288)?.split_at(32))
    }

    pub(in crate::app::epub) fn link(&self, index: u16) -> Option<Link<'_>> {
        let record = Self::record(&self.links, index, 264)?;
        let field = |at| i16::from_le_bytes([record[at], record[at + 1]]);
        Some(Link {
            href: &record[..256],
            x: field(256),
            y: field(258),
            width: field(260),
            height: field(262),
        })
    }
}
