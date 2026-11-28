#include <xp_ui_updates.h>
struct UpdateUiActivity:Activity {
 bool rustSceneRejected() const { return false; }
 XpUiUpdateState model{},painting{};XpUiUpdateFeedback feedback{};
 struct ModelLock { explicit ModelLock(UpdateUiActivity&){} };
 UpdateUiActivity(const char*n,GfxRenderer&r,MappedInputManager&i,XpUiUpdateKind k,bool recovery=false):Activity(n,r,i){xp_ui_updates_init(&model,k,recovery);}
 uint8_t phase()const{return model.phase;}
 int selectedIndex()const{return model.list.selected;}
 int paintedSelectedIndex()const{return feedback.list.selected;}
 int groupIndex()const{return model.groups.selected;}
 void notify(XpUiUpdateEvent e,uint32_t a=0,uint32_t b=0){xp_ui_updates_notify(&model,e,a,b);}
 uint8_t serviceResult(XpUiUpdateEvent e,uint32_t a=0,uint32_t b=0){return xp_ui_updates_result(&model,e,a,b);}
 struct ResourceLock {ResourceLock(UpdateUiActivity&){} ~ResourceLock(){}};
 virtual int paintedRowCount()const{return 0;}
 virtual void releaseText(){}
 bool preventAutoSleep()override{return xp_ui_updates_sleep(&model);}
 bool skipLoopDelay()override{return xp_ui_updates_delay(&model);}
 void closeRouting(){}
 virtual int rowCount()const{return 0;}virtual int familyCount()const{return 0;}
 virtual void prepareRows(){}virtual bool row(uint16_t,XpUiSettingsRow&)const{return false;}
 virtual void prepareText(){}virtual const char*dynamicText(uint32_t)const{return "";}
 virtual void afterHeader()const{}virtual const char*headerSubtitle()const{return nullptr;}
 virtual const char*fontConfirmLabel()const{return "";}
 virtual void performAction(XpUiUpdateAction,int32_t)=0;
 void loop()override{
   XpUiInput input{};input.pressed=mappedInput.pressed;input.released=mappedInput.released;input.held=mappedInput.held;
   if(mappedInput.tap)input.touch_flags=XP_UI_TOUCH_RELEASED;
   auto e=xp_ui_updates_input(&model,&input,&feedback,0,rowCount(),familyCount());
   if(e.repaint==2)requestUpdate(e.immediate);
   if(e.action)performAction(XpUiUpdateAction(e.action),e.value);
   if(e.repaint==1)requestUpdate(e.immediate);
 }
 void render(RenderLock&&)override{
   painting=model;
   XpUiCommand commands[128]{};XpUiBuffer buffer{commands,nullptr,128,0};XpUiScene scene{};
   XpUiUpdateLayout layout{};layout.list.list.screen={0,0,528,792};layout.list.list.area=layout.list.list.screen;
   layout.list.list.font=10;layout.list.help_font=10;layout.list.header_height=36;layout.list.top_padding=12;layout.list.side_padding=16;layout.list.spacing=8;
   layout.progress_height=24;layout.popup_padding=16;layout.popup_gap=8;layout.popup_side_margin=20;layout.selection_padding=8;
   XpUiUpdateResources resources{};
   resources.rows.width=[](void*,const char*t,int32_t,uint8_t)->int32_t{return strlen(t)*10;};
   resources.rows.line=[](void*,int32_t)->int32_t{return 24;};
   resources.text=[](void*,uint32_t)->const char*{return "text";};
   resources.text_height=[](void*,uint32_t,int32_t,int16_t,uint8_t,uint8_t)->int16_t{return 24;};
   if(model.kind==XP_UI_UPDATE_FONTS&&(model.phase==2||model.phase==3)){renderer.displayBuffer();return;}
   assert(xp_ui_updates_view(&painting,&layout,&resources,&buffer,&feedback,&scene)==1);
   xp_ui_updates_publish(&model,&painting,&feedback);
   if(scene.refresh)renderer.displayBuffer();
 }
};
