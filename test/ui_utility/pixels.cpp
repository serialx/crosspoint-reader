#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <FreeInkUIGfxRenderer.h>
#include <FreeInkApp.h>
#include <builtinFonts/all.h>
#include <fontIds.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "Trace.h"
#ifdef UI_UTILITY_CANDIDATE
#include <xp_ui_utility.h>
#endif
void textGoldenGlyph(const EpdFontFamily&,uint32_t,int,int,EpdFontFamily::Style){}
void textGoldenScaled(int,uint32_t,int,int,EpdFontFamily::Style){}
namespace ui=freeink::ui;
static const char* label(unsigned h){
 switch(h){case 1:return "A publication title with several words";case 3:return "Cancel";case 4:return "Confirm";
 case 10:return "Brightness";case 11:return "Warmth";case 12:return "60%";case 13:return "50%";
 case 14:return "On";case 15:return "Off";case 16:return "An author";case 17:return "Source";
 case 18:return "Version 1";case 19:return "Available";case 20:return "2 copies";case 21:return "No holds";
 case 22:return "Description";case 23:return "Some description text that wraps across lines. More content follows so the body has several paragraphs worth of readable text.";
 case 24:return "Read";case 25:return "Details";case 26:return "-";case 27:return "+";default:return "";}
}
static ui::ThemeTokens theme(ui::DrawTarget& target,int radius){
 auto t=ui::themeTokensForLineHeight(target.lineHeight(1));t.controlRadius=radius;t.capsuleRadius=radius;t.sheetRadius=radius;return t;
}
static void baselineDetails(ui::GfxRendererTarget& target,bool cover,int radius){
 const auto device=target.deviceContext();ui::InteractionBuffer<24> hits;ui::Frame<24> frame(target,device,{},hits);
 const auto t=theme(target,radius);ui::Screen<24> screen(frame,t);screen.setContentMarginFromScreen({80,0,40,0});
 ui::PublicationPageProps p;p.book.title=label(1);p.book.author=label(16);p.book.series=label(17);p.book.titleText=t.bodyText;p.book.detailText=t.bodyText;p.book.seriesLines=1;
 if(cover){p.book.coverPainter=[](ui::DrawTarget&,ui::Rect,const ui::PublicationHeaderProps&,void*){return true;};}
 else {p.book.coverSize={0,0};p.book.titleLines=2;p.book.authorLines=1;p.heroHeight=4*target.lineHeight(1)+2*p.book.titleGap+p.book.textGap;}
 p.availability.status=label(19);p.availability.copies=label(20);p.availability.holds=label(21);p.availability.headingText=t.bodyText;p.availability.detailText=t.bodyText;
 p.metadata=label(18);p.metadataLines=3;p.descriptionHeading=label(22);p.description=label(23);p.headingText=t.bodyText;p.bodyText=t.bodyText;
 for(auto* b:{&p.primary,&p.more}){b->styles=t.button;b->styles.normal.border=ui::Paint::solid(ui::Color::Black);b->styles.normal.borderWidth=1;b->text=t.bodyText;b->radius=t.controlRadius;b->minTouchSize=t.minTouchSize;}
 p.primary.label=label(24);p.primary.action=1;p.more.label=label(25);p.more.action=2;p.actionHeight=t.rowHeight;ui::publicationPage(frame,screen.body(),p);
}
static void baselineLight(ui::GfxRendererTarget& target,bool warmth,int radius){
 const auto device=target.deviceContext();ui::InteractionBuffer<24> hits;ui::Frame<24> frame(target,device,{},hits);const auto t=theme(target,radius);ui::Screen<24> screen(frame,t);
 const auto line=target.lineHeight(t.smallText.font);const int bottom=t.spaceMd+line+t.spaceMd+(line+t.spaceMd+56+2*t.spaceMd)*(warmth?2:1)+t.spaceSm+2*t.spaceLg+5+t.spaceMd;
 ui::SheetProps sheet;sheet.grabberMargin=t.spaceLg;sheet.grabberInset=t.spaceLg+t.spaceMd;screen.sheet(sheet,bottom);screen.insetContent({0,16,0,16});
 screen.spacer(t.spaceMd);screen.takeTop(line,t.spaceMd);
 for(int row=0;row<(warmth?2:1);++row){ui::SliderRowProps p;p.label=label(row?11:10);p.value=label(row?13:12);p.sliderValue=row?50:60;p.sliderAction=row?2:1;p.decrement=row?5:4;p.increment=row?5:4;
 screen.sliderRow(p,56);screen.spacer(t.spaceMd);}screen.spacer(t.spaceSm);
}
#ifdef UI_UTILITY_CANDIDATE
static void candidate(GfxRenderer& r,ui::GfxRendererTarget& target,int kind,bool variant,int radius){
 XpUiUtilityState s{};s.kind=kind;s.brightness=60;s.warmth=50;s.switch_owns_light=1;assert(xp_ui_utility_init(&s));
 const auto t=theme(target,radius);const auto safe=target.deviceContext().safeRect();XpUiUtilityLayout l{};
 l.screen={0,0,static_cast<int16_t>(r.getScreenWidth()),static_cast<int16_t>(r.getScreenHeight())};l.safe={safe.x,safe.y,safe.width,safe.height};
 l.body_font=UI_12_FONT_ID;l.small_font=UI_10_FONT_ID;l.title_font=UI_12_FONT_ID;l.readout_font=UI_12_FONT_ID;
 l.top_padding=10;l.header_height=50;l.spacing=20;l.hints_height=40;l.battery_height=0;l.space_sm=t.spaceSm;l.space_md=t.spaceMd;l.space_lg=t.spaceLg;
 l.row_height=t.rowHeight;l.min_touch=t.minTouchSize;l.control_radius=radius;l.sheet_radius=radius;l.capsule_radius=radius;l.show_cover=variant;l.light_present=1;l.has_warmth=variant;
 const XpUiMetrics metrics{&r,[](void* p,int32_t f,const uint8_t* text,uint16_t len,uint8_t style)->int32_t{return static_cast<GfxRenderer*>(p)->getTextWidth(f,std::string(reinterpret_cast<const char*>(text),len).c_str(),static_cast<EpdFontFamily::Style>(style));},[](void* p,int32_t f)->int32_t{return static_cast<GfxRenderer*>(p)->getLineHeight(f);}};
 const XpUiUtilityTexts texts{&r,[](void*,uint32_t h){return label(h);},[](void* p,uint32_t h,int32_t font,int16_t w,uint8_t bold,uint8_t lines)->int16_t{ui::GfxRendererTarget target(*static_cast<GfxRenderer*>(p));target.setFont(0,font);ui::TextStyle style;style.bold=bold;style.maxLines=lines;return ui::measureWrappedText(target,label(h),style,w).height;}};
 XpUiCommand commands[80]{};XpUiInteraction hits[16]{};const XpUiBuffer buffer{commands,hits,80,16};XpUiUtilityFeedback feedback{};XpUiScene scene{};
 assert(xp_ui_utility_view(&s,&l,&metrics,&texts,&buffer,&feedback,&scene));
 const XpUiPaint paint{&r,nullptr,nullptr,[](void*,uint32_t,uint32_t)->uint8_t{return 1;},[](void* p,const XpGlyphTarget*,const XpUiCommand* c,XpUiRect clip){if(c->kind==XP_UI_TEXT_HANDLE)static_cast<GfxRenderer*>(p)->drawUiText(*c,clip,label(c->resource));}};
 assert(r.drawUiScene(scene,&paint));
}
#endif
int main(int argc,char** argv){assert(argc==3);HalDisplay display;GfxRenderer r(display);r.begin();
 static EpdFont small(&ubuntu_10_regular),smallBold(&ubuntu_10_bold),body(&ubuntu_12_regular),bodyBold(&ubuntu_12_bold);
 r.insertFont(UI_10_FONT_ID,EpdFontFamily(&small,&smallBold));r.insertFont(UI_12_FONT_ID,EpdFontFamily(&body,&bodyBold));
 FontDecompressor decompressor;assert(decompressor.init());FontCacheManager cache(r.getFontMap(),r.getSdCardFonts(),r.getTtfFonts());cache.setFontDecompressor(&decompressor);r.setFontCacheManager(&cache);
 for(int orientation=0;orientation<4;++orientation)for(int radius:{0,12,18})for(bool variant:{false,true})for(int kind:{2,3}){
 r.setOrientation(static_cast<GfxRenderer::Orientation>(orientation));r.setClipRect(0,0,r.getScreenWidth(),r.getScreenHeight());r.clearScreen();
 ui::GfxRendererTarget target(r);target.setFont(0,UI_10_FONT_ID);target.setFont(1,UI_12_FONT_ID);target.setFont(2,UI_12_FONT_ID);
 if(std::strcmp(argv[1],"cpp")==0){if(kind==2)baselineDetails(target,variant,radius);else baselineLight(target,variant,radius);}
#ifdef UI_UTILITY_CANDIDATE
 else candidate(r,target,kind,variant,radius);
#endif
 const std::string name=std::string(argv[2])+"/"+std::to_string(kind)+"-"+std::to_string(orientation)+"-"+std::to_string(radius)+"-"+std::to_string(variant)+".bin";
 auto* f=std::fopen(name.c_str(),"wb");assert(f);std::fwrite(display.pixels.data(),1,display.pixels.size(),f);std::fclose(f);
 }
}
