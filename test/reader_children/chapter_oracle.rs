use std::io::{self, Read};
fn main() {
    let mut input = String::new();
    io::stdin().read_to_string(&mut input).unwrap();
    print!(
        "{}",
        reader_children_test::chapter_trace::compare_input(&input)
    );
}
