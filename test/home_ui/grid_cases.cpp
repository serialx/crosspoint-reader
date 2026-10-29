int main() {
  for (int width:{480,528,792,800}) for (bool touch:{false,true}) for (int count:{0,1,4,7})
  for (int selected:{0,2,7,10}) for (int progress:{-1,42,100}) for (bool opds:{false,true}) {
    std::printf("grid %d %d %d %d %d %d\n",width,touch,count,selected,progress,opds);
    Recorder target;
    GfxRenderer renderer;renderer.width=width;renderer.height=width>600?480:792;
    BoardConfig::touch=touch;
    BoardConfig::ACTIVE={touch?12:0,touch?90:100,touch?3:0,touch?80:100};
    ui::DeviceContext device;device.width=width;device.height=renderer.height;
    ui::InteractionBuffer<24> hits;
    ui::Frame<24> frame(target,device,{},hits);
    auto theme=ui::themeTokensForLineHeight(24);
    theme.bodyText.font=1;theme.smallText.font=0;theme.titleText.font=1;
    UiScreen screen(frame,theme);
    std::vector<RecentBook> books(count,{"A long title with several words","An author","cover"});
    CoverGridHomeUi grid(renderer);grid.books=&books;grid.selected=selected;grid.progress=progress;grid.hasOpds=opds;
    snprintf(grid.progressText,sizeof(grid.progressText),"%d%%",progress);
    grid.draw(screen);
    std::printf("height %d\n",grid.thumbHeight);
    for (size_t i=0;i<hits.count();++i) { auto& hit=hits.data()[i];Recorder::rect("hit",hit.rect);std::printf(" %u %d\n",hit.action,hit.value); }
  }
}
