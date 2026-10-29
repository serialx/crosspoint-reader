#define main catalogOriginalMain
#include "oracle.cpp"
#undef main
#define STR_PLUGIN_AUTH_WAITING 2
#include "plugin_original.inc"
int main() {
 for (int width : {480, 528, 792, 800}) for (int height : {160, 360, 680}) for (bool touch : {false, true})
 for (int mode=0; mode<7; ++mode) {
  std::printf("plugin %d %d %d %d\n",width,height,touch,mode);
  Recorder target;
  fui::DeviceContext device;
  device.width=width; device.height=height; device.hasTouch=touch;
  target.clip={0,0,static_cast<int16_t>(width),static_cast<int16_t>(height)};
  fui::InteractionBuffer<32> hits;
  fui::Frame<32> frame(target,device,{},hits);
  auto theme=fui::themeTokensForLineHeight(24);
  fui::Screen<32> screen(frame,theme);
  screen.setContentMarginFromScreen({48,0,36,0});
  if(mode==0) { fui::Rect qr; pluginAuthScreen(screen,"https://example.invalid/activate","TEST-ABCD",qr); Recorder::rect("qr",qr); }
  else if(mode==1) catalogCenteredBlock(screen,{{"Download complete",true},{"A synthetic plugin with a long title that wraps onto a second line"}});
  else if(mode==2) catalogCenteredBlock(screen,{{"Plugin Hub installed",true},{"Return to Plugins to open the Plugin Hub"}});
  else if(mode==3) catalogCenteredBlock(screen,{{"Not signed in"}});
  else {
   fui::ListItem rows[16];
   for(int i=0;i<16;i++) { rows[i].label="The Observatory"; rows[i].subtitle="A long synthetic description of the observatory and its many shelves, tools and reading rooms."; rows[i].value=i%2?"Installed":">"; rows[i].actionValue=i; }
   fui::ListProps props; props.items=rows; props.count=16;props.action=1;props.inputMask=fui::InputTouch;props.partialTrailingRow=true;props.valueInset=8;
   props.subtitleText=theme.smallText;props.subtitleText.maxLines=mode==4?1:mode==5?2:3;
   fui::ListNav nav;nav.reset();screen.syncListViewport(nav,props,16);screen.list(props);
   std::printf("nav %d %d %d %d\n",nav.selected.load(),nav.top,nav.drawnRows,nav.inputPageRows());
  }
 }
}
