use std::io::{self, BufRead};
use xp_ui::{
    model::Input,
    reader::percent::{Controls, Outcome, State},
    scene::{HitSink, Interaction, Rect},
    utility::{CHROME, OK, SLIDER, STEP},
};

fn hits(shift: i16) -> [Interaction; 5] {
    [
        (CHROME, 0, 0, 0, 100, 100, 1),
        (SLIDER, 0, 10, 20, 81, 20, 3),
        (STEP, -1, 10, 60, 20, 20, 1),
        (STEP, 1, 30, 60, 20, 20, 1),
        (OK, 0, 50, 60, 40, 20, 1),
    ]
    .map(|(action, value, x, y, w, h, inputs)| Interaction {
        rect: Rect {
            x: x + shift,
            y,
            w,
            h,
        },
        action,
        value,
        inputs,
        ..Default::default()
    })
}
fn main() {
    let mut state = State::new(0);
    let mut ready = false;
    let mut edge = false;
    let mut controls = Controls::default();
    for line in io::stdin().lock().lines() {
        let line = line.unwrap();
        let mut tokens = line.split_whitespace();
        let operation = tokens.next().unwrap();
        let values: Vec<i64> = tokens.map(|v| v.parse().unwrap()).collect();
        match operation {
            "new" => {
                state = State::new(values[0] as i32);
                edge = values[1] != 0;
                ready = false;
            }
            "paint" => {
                state.render_begin();
                controls = Controls::default();
                for hit in hits(values[0] as i16) {
                    controls.push(hit).unwrap();
                }
                ready = true;
            }
            "input" | "during" => {
                let during = operation == "during";
                let (shift, stage) = if during {
                    (values[0] as i16, values[1] as usize)
                } else {
                    (0, 0)
                };
                let values = if during { &values[2..] } else { &values[..] };
                let mut next = Controls::default();
                if during {
                    state.render_begin();
                    for hit in hits(shift).into_iter().take(stage) {
                        next.push(hit).unwrap();
                    }
                }
                let update = state.update(
                    Input {
                        pressed: values[0] as u32,
                        released: values[1] as u32,
                        held: values[2] as u32,
                        now_ms: values[3] as u32,
                        held_ms: values[4] as u32,
                        touch_x: values[5] as i16,
                        touch_y: values[6] as i16,
                        touch_flags: values[7] as u8,
                        swipe: values[8] as i8,
                        ..Default::default()
                    },
                    ready.then_some(controls.as_slice()),
                    edge,
                );
                let result = match update.outcome {
                    Outcome::Cancel => -1,
                    Outcome::Confirm(value) => value,
                    _ => -2,
                };
                println!("{} {} {}", state.value(), result, u8::from(update.feedback));
                if during {
                    for hit in hits(shift).into_iter().skip(stage) {
                        next.push(hit).unwrap();
                    }
                    controls = next;
                    ready = true;
                }
            }
            _ => panic!("unknown operation"),
        }
    }
}
