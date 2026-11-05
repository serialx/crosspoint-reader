struct PaintOwner {
  GfxRenderer& renderer;
  ui::GfxRendererTarget& target;
  const char* text(uint32_t handle) const {
    static const char* values[]={tr(STR_REMOTE_LABEL),tr(STR_LOCAL_LABEL),tr(STR_APPLY_REMOTE),tr(STR_UPLOAD_LOCAL),tr(STR_NO_REMOTE_MSG),tr(STR_UPLOAD_PROMPT),tr(STR_NO_CREDENTIALS_MSG),tr(STR_KOREADER_SETUP_HINT),tr(STR_UPLOAD_SUCCESS),tr(STR_ALREADY_SYNCED),tr(STR_SYNC_FAILED_MSG),tr(STR_AUTH_SUCCESS),tr(STR_ACCOUNT_CREATED),tr(STR_SYNC_READY),tr(STR_AUTH_FAILED),tr(STR_SIGNUP_FAILED)};
    switch(handle){
      case 3:return "Status message";
      case 4:return "Remote chapter";
      case 5:return "Page 3, 75.00% overall";
      case 6:return "  From: Oracle reader";
      case 7:return "Local chapter";
      case 8:return "Page 3/10, 25.00% overall";
      default:return handle>=64&&handle<80?values[handle-64]:"";
    }
  }
};
int main() {
  HalDisplay display;GfxRenderer renderer(display);renderer.begin();
  static EpdFont small(&ubuntu_10_regular),smallBold(&ubuntu_10_bold),body(&ubuntu_12_regular),bodyBold(&ubuntu_12_bold);
  renderer.insertFont(UI_10_FONT_ID,EpdFontFamily(&small,&smallBold));renderer.insertFont(UI_12_FONT_ID,EpdFontFamily(&body,&bodyBold));
  FontDecompressor decompressor;assert(decompressor.init());FontCacheManager cache(renderer.getFontMap(),renderer.getSdCardFonts(),renderer.getTtfFonts());cache.setFontDecompressor(&decompressor);renderer.setFontCacheManager(&cache);
  unsigned cases=0;
  for(int orientation=0;orientation<4;++orientation)for(bool touch:{false,true})for(int selection:{0,1,2,3})for(bool bold:{false,true})for(int phase:{2,3,4,5,6,7,8,9})for(int selected:{0,1}){
    renderer.setOrientation(static_cast<GfxRenderer::Orientation>(orientation));renderer.setClipRect(0,0,renderer.getScreenWidth(),renderer.getScreenHeight());
    ui::GfxRendererTarget target(renderer,touch);target.setFont(0,UI_10_FONT_ID);target.setFont(1,UI_12_FONT_ID);
    KOReaderSyncActivity original(renderer);original.mappedInput.touch=touch;original.theme=ui::themeTokensForLineHeight(renderer.getLineHeight(UI_12_FONT_ID));
    auto& theme=original.theme;theme.bodyText.bold=bold;theme.listSelectionStyle=static_cast<ui::SelectionStyle>(selection);theme.listRowRadius=12;theme.listInset=5;theme.listRowGap=3;
    original.state=static_cast<KOReaderSyncActivity::State>(phase);original.selectedOption=selected;original.render({});const auto expected=display.pixels;
    XpKoLayout layout{};layout.list.screen={0,0,int16_t(renderer.getScreenWidth()),int16_t(renderer.getScreenHeight())};layout.list.area=layout.list.screen;layout.safe=layout.list.screen;
    layout.header={0,10,layout.list.screen.w,38};layout.header_bottom=48;layout.hints_height=48;layout.spacing=8;
    layout.list.font=UI_12_FONT_ID;layout.small_font=UI_10_FONT_ID;layout.fixed_font=UI_10_FONT_ID;
    layout.list.row_height=touch?theme.rowHeight:metrics.listRowHeight;layout.list.row_gap=theme.listRowGap;layout.list.inset=theme.listInset;layout.list.padding=theme.listSidePadding;
    layout.list.radius=theme.listRowRadius;layout.list.selection=selection;layout.list.bold=theme.bodyText.bold;
    layout.space_xs=theme.spaceXs;layout.space_sm=theme.spaceSm;layout.space_md=theme.spaceMd;layout.space_lg=theme.spaceLg;
    layout.body_height=renderer.getLineHeight(UI_12_FONT_ID);layout.small_height=renderer.getLineHeight(UI_10_FONT_ID);layout.fixed_height=layout.small_height;layout.touch=touch;layout.has_device=1;
    XpKoState state{};state.phase=phase;state.selected=selected;
    XpUiCommand commands[32]{};XpUiInteraction hits[2]{};XpUiBuffer buffer{commands,hits,32,2};XpUiScene scene{};
    assert(xp_ui_koreader_view(&state,&layout,&buffer,&scene));PaintOwner owner{renderer,target};
    XpUiPaint paint{&owner,nullptr,nullptr,[](void*,uint32_t,uint32_t)->uint8_t{return 1;},
      [](void* p,const XpGlyphTarget*,const XpUiCommand* command,XpUiRect clip){
        auto& owner=*static_cast<PaintOwner*>(p);if(command->resource<=2)return;
        if(command->kind==XP_UI_BITMAP){const auto r=command->rect;owner.target.bitmap({r.x,r.y,r.w,r.h},ui::bitmapFromIcon(command->resource==9?icon_download_24:icon_upload_24),ui::BitmapMode::Contain,ui::Paint::solid(command->paint==1?ui::Color::White:ui::Color::Black));return;}
        auto c=*command;if(c.max_lines==0&&c.align==1)c.rect.x+=(c.rect.w-owner.renderer.getTextWidth(c.font,owner.text(c.resource),static_cast<EpdFontFamily::Style>(c.style)))/2;
        owner.renderer.drawUiText(c,clip,owner.text(c.resource));}};
    assert(renderer.drawUiScene(scene,&paint));
    if(display.pixels!=expected){unsigned diff=0;for(size_t i=0;i<expected.size();++i)diff+=expected[i]!=display.pixels[i];fprintf(stderr,"KOReader pixels differ orientation=%d touch=%d selection=%d phase=%d option=%d bytes=%u\n",orientation,touch,selection,phase,selected,diff);return 1;}
    ++cases;
  }
  printf("%u KOReader cases match original C++ pixels\n",cases);
}
