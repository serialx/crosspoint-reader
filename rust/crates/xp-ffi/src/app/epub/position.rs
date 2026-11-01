use super::{abi, EpubSession, ResourceAccess};

impl EpubSession {
    pub(super) fn section_paragraph(&self, access: &ResourceAccess<'_>, page: u16) -> Option<u16> {
        let section = self.section(access)?;
        let mut paragraph = 0;
        (unsafe {
            (self.leaves.section_paragraph_for_page)(section.as_ptr(), page, &mut paragraph)
        } != 0)
            .then_some(paragraph)
    }

    pub(super) fn section_offset(&self, access: &ResourceAccess<'_>, page: u16) -> Option<u32> {
        let section = self.section(access)?;
        let mut offset = 0;
        (unsafe { (self.leaves.section_offset_for_page)(section.as_ptr(), page, &mut offset) } != 0)
            .then_some(offset)
    }

    pub(super) fn position_with_queries(
        &self,
        paragraph: impl FnOnce(u16) -> Option<u16>,
        offset: impl FnOnce(u16) -> Option<u32>,
    ) -> abi::XpEpubPosition {
        #[cfg(feature = "app-test")]
        if let Some(hook) = self.bookmark_overrides {
            let mut position = core::mem::MaybeUninit::uninit();
            unsafe {
                (hook.position)(hook.context, position.as_mut_ptr());
                return position.assume_init();
            }
        }
        let page = self.model(|state| {
            if state.facts.section.is_some() {
                state.current.page
            } else {
                state.next_page
            }
        });
        let count = self
            .estimated_pages()
            .map(i32::from)
            .unwrap_or_else(|| self.model(|state| state.cached.pages));
        let in_bounds = || {
            self.model(|state| {
                state
                    .facts
                    .section
                    .is_some_and(|section| page >= 0 && page < i32::from(section.pages))
            })
        };
        let paragraph = if in_bounds() {
            paragraph(if page > 0 {
                (page - 1) as u16
            } else {
                page as u16
            })
        } else {
            None
        };
        let mut position = abi::XpEpubPosition {
            spine: self.model(|state| state.current.spine),
            page,
            count,
            visible_offset: 0,
            paragraph: 0,
            list_item: 0,
            has_visible_offset: 0,
            has_paragraph: 0,
            has_list_item: 0,
            resolved_spine: 1,
            mapped_page: 1,
            anchor: [0; 64],
        };
        if in_bounds() {
            if let Some(offset) = offset(page as u16) {
                position.visible_offset = offset;
                position.has_visible_offset = 1;
            }
        }
        if let Some(paragraph) = paragraph {
            position.paragraph = paragraph;
            position.has_paragraph = 1;
        }
        position
    }

    pub(super) fn current_position(&self, access: &ResourceAccess<'_>) -> abi::XpEpubPosition {
        self.position_with_queries(
            |page| self.section_paragraph(access, page),
            |page| self.section_offset(access, page),
        )
    }
}
