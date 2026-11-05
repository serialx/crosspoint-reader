// Compiled with the existing SDK recording target by test_oracle.py.
int main() {
  static const uint8_t iconBytes[128]{};
  const ui::BitmapRef icon{iconBytes, 32, 32};
  for (int width : {480, 528, 792, 800}) {
    for (int selected : {-1, 0, 3, 18}) {
      for (int kind : {0, 1, 2}) {
        for (auto selection :
             {ui::SelectionStyle::InvertFill, ui::SelectionStyle::LightPill, ui::SelectionStyle::Triangle}) {
          std::printf("list %d %d %d %u\n", width, selected, kind, unsigned(selection));
          Recorder target;
          ui::DeviceContext device;
          device.width = width;
          device.height = 400;
          ui::InteractionBuffer<32> hits;
          ui::Frame<32> frame(target, device, {}, hits);
          auto theme = ui::themeTokensForLineHeight(24);
          theme.listSelectionStyle = selection;
          theme.listRowRadius = 12;
          theme.listRowGap = 6;
          theme.listInset = 5;
          ui::Screen<32> screen(frame, theme);
          ui::ListItem rows[19];
          for (int i = 0; i < 19; ++i) {
            rows[i].label = i % 3 == 1 ? "A long book title with enough words to wrap across the filename row" : "Book";
            rows[i].actionValue = i;
            rows[i].icon = icon;
            if (kind == 0) rows[i].value = ".epub";
            if (kind == 1) rows[i].subtitle = "An author name";
            if (kind > 0 && i % 3 == 0) rows[i].sectionHeading = "Group heading";
          }
          ui::ListNav nav;
          nav.reset(selected);
          ui::ListProps props;
          props.items = rows;
          props.count = 19;
          props.action = 1;
          props.inputMask = ui::InputTouch | ui::InputLongPress;
          props.partialTrailingRow = true;
          props.headerUnderline = false;
          if (kind == 0) {
            props.labelText = theme.smallText;
            props.labelText.maxLines = 2;
            props.valueInset = 8;
            props.balanceWrappedLabelWithValue = false;
          }
          screen.syncListViewport(nav, props, 19);
          screen.list(props);
          std::printf("nav %d %d %d %d\n", nav.selected.load(), nav.top, nav.drawnRows, nav.inputPageRows());
          for (size_t i = 0; i < hits.count(); ++i) {
            auto& hit = hits.data()[i];
            Recorder::rect("hit", hit.rect);
            std::printf(" %u %d\n", hit.action, hit.value);
          }
        }
      }
    }
  }
  for (int width : {480, 528, 792, 800}) {
    for (int count : {0, 1, 6}) {
      for (int selected : {-1, 0, 5}) {
        std::printf("covers %d %d %d\n", width, count, selected);
        Recorder target;
        ui::DeviceContext device;
        device.width = width;
        device.height = 600;
        ui::InteractionBuffer<32> hits;
        ui::Frame<32> frame(target, device, {}, hits);
        auto theme = ui::themeTokensForLineHeight(24);
        ui::Screen<32> screen(frame, theme);
        ui::BookCardProps card;
        card.title = "A long title with several words";
        card.author = "An author";
        card.progressLabel = "42%";
        card.centerTextOnCover = true;
        card.progress = 42;
        card.progressMax = 100;
        card.action = 1;
        card.styles = theme.listRow;
        card.titleText = theme.bodyText;
        card.titleText.maxLines = 2;
        card.authorText = card.progressText = theme.smallText;
        card.progressHeight = 6;
        card.padding = {6, 6, 6, 6};
        card.gap = theme.spaceLg + theme.spaceSm;
        card.coverSize = {90, 135};
        ui::bookCard(frame, {12, 20, static_cast<int16_t>(width - 24), 160}, card);
        ui::CoverGridProps grid;
        grid.count = count;
        grid.columns = 3;
        grid.columnLayout = ui::CoverGridColumnLayout::SpaceBetween;
        grid.coverSize = card.coverSize;
        grid.rowHeight = 147;
        grid.gap = grid.rowGap = 5;
        grid.action = 1;
        grid.inputMask = ui::InputTouch;
        grid.selectedIndex = selected;
        grid.selectionIndicator = ui::CoverGridSelectionIndicator::CoverFrame;
        grid.selectedCoverFrameGap = 6;
        grid.selectedCoverFrameWidth = 8;
        grid.cellInset = {6, 6, 6, 6};
        grid.cellStyles = card.styles;
        grid.cellStyles.selected.border = ui::Paint::dither(ui::Color::LightGray);
        grid.labelHeight = grid.labelGap = 0;
        grid.scrollIndicator = false;
        grid.itemProvider = [](uint16_t index, void*) { return ui::coverGridItem(nullptr, index + 1); };
        ui::coverGrid(frame, {12, 190, static_cast<int16_t>(width - 24), 299}, grid);
        ui::TabItem tabs[5];
        for (int i = 0; i < 5; ++i) {
          tabs[i].value = i + count;
          tabs[i].selected = i == selected;
          tabs[i].icon = icon;
        }
        ui::TabBarProps props;
        props.tabs = tabs;
        props.count = 5;
        props.layout = ui::TabBarLayout::SpaceBetween;
        props.action = 2;
        props.inputMask = ui::InputTouch;
        props.iconSize = 32;
        props.tabStyles.explicitlySet = true;
        props.selectedUnderline = 2;
        props.distributedSlotWidth = 0;
        ui::tabBar(frame, {12, 510, static_cast<int16_t>(width - 24), 50}, props);
        for (size_t i = 0; i < hits.count(); ++i) {
          auto& hit = hits.data()[i];
          Recorder::rect("hit", hit.rect);
          std::printf(" %u %d\n", hit.action, hit.value);
        }
      }
    }
  }
}
