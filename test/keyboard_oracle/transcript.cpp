void KeyboardEntryActivity::render(RenderLock&&) {}
bool KeyboardEntryActivity::cursorPositionFromPoint(int, int, size_t&) const { return false; }

void hex(const std::string& s) {
  for(unsigned char c:s) std::printf("%02x",c);
  std::printf("-");
}
void dump(const KeyboardEntryActivity& k) {
  hex(k.text); hex(k.displayTextForCurrentState()); hex(k.returned);
  std::printf(" %zu %u %d %d %d %d %d %d %d %d %d %u %d\n", k.cursorPos, unsigned(k.layoutId),
    k.shifted,k.symbols,k.urlPanel,k.cursorMode,k.togglePos,k.passwordVisible,k.selRow,k.selCol,
    k.hintVisible,k.repaints,k.result);
}
void edge(KeyboardEntryActivity& k, MappedInputManager::Button button, unsigned phase, uint32_t duration=0) {
  auto& in=k.mappedInput;
  in={}; now+=100;
  const uint32_t mask=1u<<unsigned(button);
  in.pressed=phase==0?mask:0; in.released=phase==2?mask:0; in.held=phase!=2?mask:0; in.duration=duration;
  k.loop();dump(k);
}
int main() {
  GfxRenderer renderer; MappedInputManager input;
  ButtonNavigator::setMappedInputManager(input);
  for(unsigned lang=0;lang<10;lang++) for(unsigned type=0;type<3;type++) for(unsigned limit:{0,1,4,12}) {
    SETTINGS.keyboardLayouts=1023;
    I18N.language=keyboard_layouts::ALL[lang].language;
    KeyboardEntryActivity k(renderer,input,"Title","",limit,static_cast<InputType>(type));
    k.onEnter();
    std::printf("case %u %u %u\n",lang,type,limit);dump(k);
    // Every real key, shifted/symbol pages, alt output and byte limits.
    for(int layer=0;layer<4;layer++) {
      k.symbols=layer>=2;k.shifted=layer%2;
      const auto& layout=k.currentLayout();
      for(unsigned row=0;row<layout.rowCount;row++) for(unsigned col=0;col<layout.rows[row].count;col++) {
        const auto& key=layout.rows[row].keys[col];
        if(key.value<0 || key.value==13 || key.value==8) continue;
        for(bool alt:{false,true}) {
          k.text.clear();k.cursorPos=0;k.symbols=layer>=2;k.shifted=layer%2;
          k.activateValue(key.value,alt);dump(k);
        }
      }
    }
    k.symbols=false;k.shifted=false;k.text="aéЖאع";k.cursorPos=k.text.size();
    for(int i=0;i<7;i++) {k.backspaceUtf8();dump(k);}
    k.text="aéЖאع";k.cursorPos=3;k.insertUtf8("ø");dump(k);k.backspaceUtf8();dump(k);
    for(int i=0;i<12;i++){k.activateValue(-4,false);dump(k);}
    for(int value:{-1,-2,-1,-2,-3,2001,2007,-3,8,8,13}) {k.activateValue(value,false);dump(k);}
    edge(k,MappedInputManager::Button::Up,0);edge(k,MappedInputManager::Button::Up,1,501);edge(k,MappedInputManager::Button::Up,2);
    edge(k,MappedInputManager::Button::Left,0);edge(k,MappedInputManager::Button::Left,2);
    edge(k,MappedInputManager::Button::Right,0);edge(k,MappedInputManager::Button::Right,1,501);edge(k,MappedInputManager::Button::Right,2);
    edge(k,MappedInputManager::Button::Confirm,0);edge(k,MappedInputManager::Button::Confirm,2);
    edge(k,MappedInputManager::Button::Down,0);edge(k,MappedInputManager::Button::Down,2);
    for(auto b:{MappedInputManager::Button::Left,MappedInputManager::Button::Right,MappedInputManager::Button::Down,MappedInputManager::Button::Up,MappedInputManager::Button::Confirm}) {
      edge(k,b,0);edge(k,b,1,500);edge(k,b,1,501);edge(k,b,1,1501);edge(k,b,2);
    }
    k.syncSelectionToValue(8);k.text="clear me";k.cursorPos=k.text.size();
    edge(k,MappedInputManager::Button::Confirm,0);edge(k,MappedInputManager::Button::Confirm,1,1500);edge(k,MappedInputManager::Button::Confirm,1,1501);edge(k,MappedInputManager::Button::Confirm,2);
    edge(k,MappedInputManager::Button::Back,2);
  }
  for(unsigned mask=0;mask<65536;mask++) {
    SETTINGS.keyboardLayouts=mask;
    for(auto lang:{Language::EN,Language::RU,Language::AR,static_cast<Language>(255)}) {
      I18N.language=lang;
      std::printf("mask %u %u %u %u",mask,unsigned(lang),keyboard_layouts::enabled(),unsigned(keyboard_layouts::startingLayout()));
      for(unsigned id=0;id<11;id++) std::printf(" %u",unsigned(keyboard_layouts::next(static_cast<freeink::ui::KeyboardLayoutId>(id))));
      std::puts("");
    }
  }
}
