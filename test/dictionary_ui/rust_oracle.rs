use xp_ui::{dictionary::{definition,selection::{self,Layout,State,Word}},scene::Rect};
fn rect(r:Rect) { print!(" {} {} {} {}",r.x,r.y,r.w,r.h); }
fn main() {
    for width in [480,528,792,800] {
        for orientation in 0..4 {
            for theme in 0..3 {
                let height=if width>528 {480}else{792};
                let definition=definition::Layout {screen:Rect{x:0,y:0,w:width,h:height},top:7,right:11,bottom:13,left:17,
                    top_padding:12,header_height:32,spacing:8+theme*2,side_padding:12+theme*4,hints_height:40,side_hints_width:36,
                    orientation,..Default::default()};
                let body=definition::body(&definition);
                println!("body {width} {orientation} {theme} {} {} {} {}",body.x,body.y,body.w,body.h);
                for touch in [0,1] {
                    for rtl in [0,1] {
                        let words:Vec<_>=(0..240).map(|i|Word{x:-4+i%8*64,y:4+(i/8%10)*62,width:24+i%3*10,height:24,
                            row:(i/8) as u16,pageOffset:(i/80) as u8,isRtl:rtl,text:c"word".as_ptr(),
                            ..unsafe{core::mem::zeroed()}}).collect();
                        let layout=Layout {screen:definition.screen,safe:Rect{x:5,y:9,w:width-14,h:height-51},top:7,right:11,bottom:13,left:17,
                            spacing:8+theme*2,padding:6+theme*2,top_padding:12,line_height:20,label_width:56,label_height:20,touch,..Default::default()};
                        let mut state=State{row_count:30,..Default::default()};let mut updates=0;
                        for selected in [0,1,7,40,79,80,159,239] {
                            state.selected=selected;state.page=i32::from(words[selected as usize].pageOffset);
                            state.range_start=if selected>0 {selected-1}else{0};state.moved=1;
                            for mode in [0,2] {
                                state.mode=mode;let (x,y)=state.offsets(&words,&layout);
                                print!("selection {selected} {touch} {rtl} {mode} {x} {y}");
                                rect(state.handle(&words,&layout,state.range_start as usize,true));
                                rect(state.handle(&words,&layout,selected as usize,false));rect(state.actions(&words,&layout));
                                println!(" next {}",state.next_page(&words).map_or(-1,|v|v as i32));
                                let word=words[selected as usize];
                                for dx in [-9,-4,-3,0,24,58,75] {
                                    let x=i32::from(word.x)+dx;let y=i32::from(word.y)+10+state.offsets(&words,&layout).1;
                                    println!("hit {} {} {} {} {}",state.word_at(&words,&layout,x,y,false).map_or(-1,|v|v as i32),
                                        state.word_at(&words,&layout,x,y,true).map_or(-1,|v|v as i32),i32::from(state.contains(&words,&layout,x,y)),
                                        state.action_at(&words,&layout,x,y).map_or(-1,i32::from),i32::from(state.dwell(&words,&layout,x,y)));
                                }
                            }
                            updates+=i32::from(state.vertical(&words,-1));updates+=i32::from(state.vertical(&words,1));
                            updates+=i32::from(state.move_page(&words,state.page+1));
                            println!("move {} {} {updates}",state.selected,state.page);
                        }
                    }
                }
            }
        }
    }
}
