#![allow(dead_code)]
mod model {pub struct Input {pub pressed:u32,pub held:u32} pub const BACK:u32=1;}
mod web;
use std::ffi::c_void;
struct Context {now:u32, running_calls:u32, checks:u32}
unsafe extern "C" fn call(context:*mut c_void,operation:u32,_:u32)->u32 {
 let c=unsafe{&mut *context.cast::<Context>()};
 match operation {
  0=>c.now,
  28=>{c.running_calls+=1;u32::from(c.running_calls==1)},
  35=>{c.checks+=1;1},
  32=>(-50_i32) as u32,
  _=>0,
 }
}
unsafe extern "C" fn publish(_: *mut c_void,_:*const web::State) {}
fn main() {
 for current in -1..=5 {for rssi in -100..=0 {
  println!("rssi:{current}:{rssi}:{}",web::bars_for_rssi(rssi,current));
 }}
 let mut context=Context{now:0,running_calls:0,checks:0};
 let mut services=web::Services{context:(&mut context as *mut Context).cast(),call:Some(call),publish:Some(publish)};
 // Fresh instances share the original process-lifetime health-check timestamp.
 for (now,checks) in [(1999,0),(2000,0),(2001,1),(4001,1),(4002,2),(u32::MAX-1000,3),(999,3),(1000,4)] {
  context.now=now;context.running_calls=0;
  let mut state=web::State{phase:3,..Default::default()};
  state.run(&mut services,2,0);assert_eq!(context.checks,checks);
 }
 for (pressed,held,home) in [(1,0,false),(0,1,false),(0,0,true)] {
  let mut latch=0;
  assert!(web::cancel(&mut latch,model::Input{pressed,held},home));
  assert!(web::cancel(&mut latch,model::Input{pressed:0,held:0},false));
 }
 let mut latch=0;assert!(!web::cancel(&mut latch,model::Input{pressed:0,held:0},false));
}
