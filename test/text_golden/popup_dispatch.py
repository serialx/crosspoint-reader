"""Compile actual shipping popup/restart dispatch and native restart phases."""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(sys.argv[1]).resolve()
sys.path.insert(0, str(root / 'test/reader_lifecycle'))
from test_popup_resource import definition, native_leaf
popup = (root / 'rust/crates/xp-ffi/src/app/popup.rs').read_text()
recovery = (root / 'rust/crates/xp-ffi/src/app/epub/recovery.rs').read_text()
code = r'''
#![allow(dead_code)]
use std::cell::{Cell,RefCell};
thread_local! {static EVENTS: RefCell<Vec<&'static str>>=const{RefCell::new(Vec::new())};static CANCEL: Cell<bool>=const{Cell::new(false)};}
fn event(v:&'static str){EVENTS.with(|e|e.borrow_mut().push(v));}
mod xp_ui {pub mod scene {#[repr(u8)]pub enum Refresh {Fast=1}}}
#[repr(C)]#[derive(Clone,Copy,Default,Debug,PartialEq)]struct Rect{x:i16,y:i16,w:i16,h:i16}
#[repr(C)]#[derive(Clone,Copy)]struct Text{data:*const u8,len:usize}
impl Text {unsafe fn bytes(&self)->Option<&[u8]>{Some(std::slice::from_raw_parts(self.data,self.len))}}
#[repr(C)]#[derive(Default)]struct Facts {marker:u32}
struct Resources{popup_facts:unsafe extern "C" fn(*mut Facts),refresh:unsafe extern "C" fn(u8)}
impl Resources {METHODS
unsafe fn translate(&self,_:u16)->Text{Text{data:b"Loading".as_ptr(),len:7}}
}
unsafe fn target()->(u32,bool){event("target");(42,true)}
unsafe fn paint(f:&Facts,text:&[u8],target:&mut u32,cache:bool)->Rect{
 assert_eq!(f.marker,99);assert_eq!(*target,42);assert!(cache);assert_eq!(text,b"Loading");event("paint");Rect{x:1,y:2,w:3,h:4}}
unsafe fn progress(f:&Facts,r:Rect,p:i32,target:&u32){assert_eq!(f.marker,99);assert_eq!(*target,42);assert_eq!(p,140);assert_eq!(r.w,0);event("progress");}
unsafe extern "C" fn facts(out:*mut Facts){event("facts");out.write(Facts{marker:99});}
unsafe extern "C" fn refresh(mode:u8){assert_eq!(mode,1);event("refresh");}
unsafe extern "C" fn restart(finish:u8)->u8{event(if finish==0{"prepare"}else{"finish"});u8::from(!CANCEL.with(Cell::get))}
struct Leaves{restart_reader:unsafe extern "C" fn(u8)->u8}
struct Session{leaves:Leaves,resources:Resources}
mod ids {pub const STR_LOADING_POPUP:u16=7;}
#[repr(u8)]enum RestartPhase {Prepare,Finish}
impl Session{unsafe fn restart(&self){RESTART}}
fn take()->Vec<&'static str>{EVENTS.with(|e|std::mem::take(&mut*e.borrow_mut()))}
fn main(){unsafe{
 let s=Session{leaves:Leaves{restart_reader:restart},resources:Resources{popup_facts:facts,refresh}};
 assert_eq!(s.resources.popup_draw(s.resources.translate(7)),Rect{x:1,y:2,w:3,h:4});
 assert_eq!(take(),["facts","target","paint","refresh"]);
 s.resources.popup_progress(Rect::default(),140);assert_eq!(take(),["facts","target","progress","refresh"]);
 s.restart();assert_eq!(take(),["prepare","facts","target","paint","refresh","finish"]);
 CANCEL.with(|v|v.set(true));s.restart();assert_eq!(take(),["prepare"]);
}}
'''.replace('METHODS', definition(popup, '    pub unsafe fn popup_draw(')+'\n'+definition(popup, '    pub unsafe fn popup_progress(')).replace('RESTART', definition(recovery, '            if (self.leaves.restart_reader)(RestartPhase::Prepare as u8)'))
main = (root / 'src/main.cpp').read_text()
leaf_source = (root / 'src/activities/reader/RustEpubResources.cpp').read_text()
start = leaf_source.index('#ifdef XP_APP_TEST\n      .restart_reader')
leaf_source = leaf_source[leaf_source.index('#else', start):leaf_source.index('#endif', start)]
native = r'''
#include <cassert>
#include <cstdint>
#include <vector>
#include <string>
#include "xp_epub_session.h"
std::vector<std::string> events;
bool deepSleepInProgress=false;
uint32_t silentRebootTarget=0,silentRebootPayload=0,silentRebootMagic=0;
constexpr uint32_t SILENT_REBOOT_TARGET_READER=2,SILENT_REBOOT_LIGHT_ON=4,SILENT_REBOOT_MAGIC=8;
struct {bool on=false;bool isOn(){events.push_back("light");return on;}} Frontlight;
struct {void restart(){events.push_back("restart");}} ESP;
void delay(int ms){assert(ms==50);events.push_back("delay");}
#define LOG_DBG(...) events.push_back("log")
BODIES
static auto leaf=LEAF;
int main(){
 for(bool sleep:{false,true})for(bool light:{false,true}){
  deepSleepInProgress=sleep;Frontlight.on=light;silentRebootMagic=0;events.clear();
  assert(leaf(255)==0 && events.empty());
  bool prepared=leaf(XP_EPUB_RESTART_PREPARE);
  assert(prepared==!sleep);
  if(sleep){assert(events.empty());assert(silentRebootMagic==0);continue;}
  assert((events==std::vector<std::string>{"light","log"}));
  assert(silentRebootMagic==SILENT_REBOOT_MAGIC && silentRebootTarget==SILENT_REBOOT_TARGET_READER);
  assert(silentRebootPayload==(light?SILENT_REBOOT_LIGHT_ON:0));
  events.push_back("rust-popup");leaf(XP_EPUB_RESTART_FINISH);
  assert((events==std::vector<std::string>{"light","log","rust-popup","delay","restart"}));
 }
}
'''.replace('BODIES','\n'.join(definition(main,signature)for signature in ['static void armSilentReboot(','static bool prepareSilentRestartTo(','bool prepareSilentRestartToReader(','void finishSilentRestart('])).replace('LEAF',native_leaf(leaf_source,'restart_reader'))
with tempfile.TemporaryDirectory(prefix='popup-shipping-') as directory:
    out=Path(directory)
    (out/'dispatch.rs').write_text(code)
    subprocess.run(['rustc','--edition=2021','-Dwarnings',str(out/'dispatch.rs'),'-o',str(out/'rust')],check=True)
    subprocess.run([str(out/'rust')],check=True)
    (out/'native.cpp').write_text(native)
    subprocess.run(['c++','-std=c++20','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-I',str(root/'rust/crates/xp-ffi/include'),str(out/'native.cpp'),'-o',str(out/'native')],check=True)
    subprocess.run([str(out/'native')],check=True)
