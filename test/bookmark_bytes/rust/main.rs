use bookmark_owner_tests::bookmarks::{decode, encode, Record};
use std::io::{self, BufRead};

fn bytes(text: &str) -> Vec<u8> {
    text.as_bytes()
        .as_chunks::<2>()
        .0
        .iter()
        .map(|pair| u8::from_str_radix(std::str::from_utf8(pair).unwrap(), 16).unwrap())
        .collect()
}

fn main() {
    let mut records = Vec::new();
    for line in io::stdin().lock().lines() {
        let line = line.unwrap();
        if let Some(input) = line.strip_prefix("load\t") {
            match decode::<Vec<u8>>(&bytes(input)) {
                Err(error) => println!("error:{error:?}"),
                Ok(rows) => {
                    print!("ok");
                    for row in rows {
                        print!(";");
                        for field in [&row.xpath, &row.summary, &row.name] {
                            for byte in field {
                                print!("{byte:02x}");
                            }
                            print!("\t");
                        }
                        print!(
                            "{}\t{}\t{}\t{}\t",
                            row.percentage.to_bits(),
                            row.spine,
                            row.page_count,
                            row.page
                        );
                        match row.visible_offset {
                            Some(n) => print!("{n}"),
                            None => print!("-"),
                        }
                    }
                    println!();
                }
            }
            continue;
        }
        if line == "save" {
            match encode(&records) {
                Ok(data) => {
                    for byte in data {
                        print!("{byte:02x}");
                    }
                    println!();
                }
                Err(error) => println!("error:{error:?}"),
            }
            records.clear();
            continue;
        }
        let fields: Vec<_> = line.split('\t').collect();
        assert_eq!(fields.len(), 8);
        records.push(Record {
            xpath: bytes(fields[0]),
            summary: bytes(fields[1]),
            name: bytes(fields[2]),
            percentage: f32::from_bits(fields[3].parse().unwrap()),
            spine: fields[4].parse().unwrap(),
            page_count: fields[5].parse().unwrap(),
            page: fields[6].parse().unwrap(),
            visible_offset: if fields[7] == "-" {
                None
            } else {
                Some(fields[7].parse().unwrap())
            },
        });
    }
}
