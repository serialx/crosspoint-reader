int main(){for(int width:{480,528,792,800})for(bool touch:{false,true})for(int themeId:{0,1,2})for(int selected:{0,1})for(int active:{0,1,2})for(bool descending:{false,true}){
 std::printf("tabs %d %d %d %d %d %d\n",width,touch,themeId,selected,active,descending);
 Recorder target;ui::DeviceContext device;device.width=width;device.height=792;device.hasTouch=touch;
 ui::InteractionBuffer<24> hits;ui::Frame<24> frame(target,device,{},hits);auto theme=ui::themeTokensForLineHeight(24);
 theme.listRowRadius=themeId==2?20:themeId==1?6:0;theme.bodyText.bold=themeId==2;
 UiScreen screen(frame,theme);screen.setContentMarginFromScreen({94,0,40,0});
 UITheme::getInstance().metrics={themeId==2,themeId==1?8:10,themeId==1?48:50,themeId==1?3:0,20,themeId==1?16:10};
 UiTabListActivity tabs;tabs.mappedInput.touch=touch;tabs.selected=selected;tabs.active=active;tabs.descending=descending;tabs.buildTabBar(screen);
 Recorder::rect("body",screen.body());std::printf("\n");
 for(size_t i=0;i<hits.count();++i){auto& h=hits.data()[i];Recorder::rect("hit",h.rect);std::printf(" %u %d\n",h.action,h.value);}
}}
