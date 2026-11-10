use super::*;
use std::ffi::CStr;

pub struct Directory {
    pub families: usize,
    pub mixed: bool,
    pub sizes: usize,
    pub only_vector: bool,
    root: usize,
    file: usize,
    family: usize,
}
impl Directory {
    pub fn new(families: usize) -> Self {
        Self {
            families,
            sizes: 3,
            mixed: false,
            only_vector: false,
            root: 0,
            file: 0,
            family: 0,
        }
    }
}
unsafe impl catalog::Directory for Directory {
    fn open(&mut self, path: &CStr, root: bool) -> i32 {
        if root {
            self.root = 0;
            return i32::from(path == c"/.fonts");
        }
        self.file = 0;
        let text = path.to_str().unwrap();
        self.family = text
            .rsplit('/')
            .next()
            .unwrap()
            .trim_start_matches('F')
            .parse()
            .unwrap();
        1
    }
    fn next(&mut self, root: bool, name: &mut [u8; 128], directory: &mut bool) -> bool {
        let text = if root {
            if self.root == self.families {
                return false;
            }
            let text = format!("F{}", self.root);
            self.root += 1;
            *directory = true;
            text
        } else {
            *directory = false;
            let count = if self.only_vector {
                6
            } else if self.mixed && self.family + 1 == self.families {
                65
            } else {
                self.sizes
            };
            if self.file == count {
                return false;
            }
            let text = if self.only_vector {
                [
                    "Regular.ttf",
                    "Bold.ttf",
                    "Italic.ttf",
                    "BoldItalic.ttf",
                    "Medium.ttf",
                    "Black.ttf",
                ][self.file]
                    .to_owned()
            } else if self.mixed && self.family + 1 == self.families && self.file != 0 {
                format!("Face{:02}-abcdefghijklmnopqrstuvwxyz012.ttf", self.file)
            } else {
                format!("F{}_{}.cpfont", self.family, 14 + self.file * 2)
            };
            self.file += 1;
            text
        };
        name.fill(0);
        name[..text.len()].copy_from_slice(text.as_bytes());
        true
    }
    fn close(&mut self, _: bool) {}
    fn inspect(&mut self, _: &CStr) -> Option<(u16, bool)> {
        None
    }
    fn log(&mut self, _: u32, _: &CStr, _: Option<&CStr>, _: usize, _: usize) {}
}
pub fn snapshot() -> String {
    use std::fmt::Write;
    let mut directory = Directory::new(9);
    directory.mixed = true;
    directory.sizes = 1;
    let mut catalog = unsafe { catalog::Catalog::new(&ALLOCATOR) };
    assert_eq!(catalog.discover(&mut directory, true), Ok(true));
    let mut text = String::new();
    for family in catalog.families() {
        writeln!(
            &mut text,
            "FAMILY {} {} {}",
            family.name().to_str().unwrap(),
            u8::from(family.vector),
            family.files().len()
        )
        .unwrap();
        for file in family.files() {
            writeln!(
                &mut text,
                "FILE {} {} {}",
                file.name().map_or("canonical", |n| n.to_str().unwrap()),
                file.size,
                file.style
            )
            .unwrap();
        }
    }
    text
}
