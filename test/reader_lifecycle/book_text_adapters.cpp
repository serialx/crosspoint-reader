// Actual native adapter bodies below call physical boundaries that record their C-string reads.
#undef Storage
namespace BookTextAdapters {
struct Renderer;
static std::string read(const char*);
}  // namespace BookTextAdapters
namespace freeink::ui {
class GfxRendererTarget : public BookTextUi::TextTarget {
 public:
  explicit GfxRendererTarget(BookTextAdapters::Renderer&) {}
  Size measureText(FontId font, const char* value, TextStyle style) const override {
    BookTextAdapters::read(value);
    return TextTarget::measureText(font, value, style);
  }
};
}  // namespace freeink::ui
namespace BookTextAdapters {
static std::vector<std::string> reads;
static std::string read(const char* value) {
  reads.emplace_back(value ? value : "");
  return reads.back();
}
struct HalFile {};
struct StorageType {
  bool exists(const char* s) {
    read(s);
    return true;
  }
  bool removeDir(const char* s) {
    read(s);
    return true;
  }
  bool remove(const char* s) {
    read(s);
    return true;
  }
  bool mkdir(const char* s) {
    read(s);
    return true;
  }
  bool rename(const char* a, const char* b) {
    read(a);
    read(b);
    return true;
  }
  bool openFileForRead(const char* origin, const char* path, HalFile&) {
    read(origin);
    read(path);
    return true;
  }
  bool openFileForWrite(const char* origin, const char* path, HalFile&) {
    read(origin);
    read(path);
    return true;
  }
} Storage;
struct InflateStream {
  static void logAllocationFailure(const char* s, uint32_t) { read(s); }
};
struct FontSystem {
  int& registry() {
    static int value;
    return value;
  }
} sdFontSystem;
static std::array<uint8_t, 1> readerFontPointSizes(int*, const char* s) {
  read(s);
  return {12};
}
struct EpdFontFamily {
  enum Style { Regular = 0, BOLD = 1 };
};
struct Renderer {
  int getTextAdvanceX(int, const char* s, EpdFontFamily::Style, int) { return int(read(s).size()); }
  int getTextWidth(int, const char* s, EpdFontFamily::Style) { return int(read(s).size()); }
  void drawUiText(const XpUiCommand&, XpUiRect, const char* s) { read(s); }
  std::vector<std::string> wrappedText(int, const char* s, int, int, EpdFontFamily::Style) { return {read(s)}; }
  std::string truncatedText(int, const char* s, int, EpdFontFamily::Style) { return read(s); }
} renderer;
static auto* target = &renderer;
struct Rectangle {
  int x, y, w, h;
};
Rectangle rect(XpUiRect r) { return {r.x, r.y, r.w, r.h}; }
XpUiRect rect(Rectangle r) { return {int16_t(r.x), int16_t(r.y), int16_t(r.w), int16_t(r.h)}; }
struct Gui {
  void drawHeader(Renderer&, Rectangle, const char* s, const char*, bool) { read(s); }
  Rectangle drawPopup(Renderer&, const char* s) {
    read(s);
    return {};
  }
  void drawButtonHints(Renderer&, const char*, const char*, const char*, const char*) {}
} GUI;
struct Mapped {
  struct Labels {
    const char* btn1;
    const char* btn2;
    const char* btn3;
    const char* btn4;
  };
  Labels mapLabels(const char* a, const char* b, const char* c, const char* d) {
    read(a);
    read(b);
    read(c);
    read(d);
    return {a, b, c, d};
  }
} mappedValue;
static auto* mapped = &mappedValue;
struct OptionPopup {
  template <class Label, class Choose>
  void show(const char* title, const char* message, int count, int, Label label, Choose) {
    read(title);
    read(message);
    for (int i = 0; i < count; ++i) {
      std::string value;
      label(i, value);
    }
  }
};
struct Clippings {
  bool addClipping(uint16_t, uint16_t, uint16_t, uint16_t, uint16_t, uint16_t, uint32_t, const char* chapter, uint32_t,
                   const std::string&, uint64_t, uint32_t, uint32_t) {
    read(chapter);
    return true;
  }
} CLIPPINGS;
namespace pluginevents {
enum class Event { ReaderOpen, ReaderExit, ReaderSession };
struct Var {
  const char* name;
  const char* value;
};
void emit(Event, const Var* vars, size_t size) {
  for (size_t i = 0; i < size; ++i)
    if (std::string_view(vars[i].name) == "book" || std::string_view(vars[i].name) == "document") read(vars[i].value);
}
}  // namespace pluginevents
enum class BmpReaderError { Ok };
struct BitmapReader {
  HalFile file;
  struct Bitmap {
    BmpReaderError parseHeaders() { return BmpReaderError::Ok; }
  } bitmap;
  bool parsed;
};
// ACTUAL_TEXT_ADAPTERS
static void verify(XpAppText value, const std::string& bytes) {
  const auto expected = bytes.substr(0, bytes.find('\0'));
  auto check = [&](auto call, size_t count) {
    reads.clear();
    call();
    assert(reads.size() == count);
    for (const auto& got : reads) assert(got == expected);
  };
  check([&] { path_remove_tree(value); }, 1);
  check([&] { path_remove(value); }, 1);
  check([&] { path_mkdir(value); }, 1);
  check([&] { path_rename(value, value); }, 2);
  check([&] { delete static_cast<HalFile*>(file_open_read(value, value)); }, 2);
  check([&] { delete static_cast<HalFile*>(file_open_write(value, value)); }, 2);
  check([&] { font_sizes(value, [](void*, XpAppText) -> uint8_t { return 1; }, nullptr); }, 1);
  check([&] { font_text_advance(value, 0, 0, 0); }, 1);
  check([&] { inflate_oom_log(value, 1); }, 1);
  check([&] { emit_open(value); }, 1);
  check([&] { emitExit(value, 50); }, 1);
  check(
      [&] {
        XpEpubSessionEvent event{};
        emitSession(value, value, &event);
      },
      2);
  check(
      [&] {
        OptionPopup popup;
        popup_show(&popup, value, value, 0, 0, nullptr, nullptr, nullptr, nullptr);
      },
      2);
  check(
      [&] {
        std::string contents = "clipping";
        XpEpubClipInsert row{};
        row.chapter = value;
        row.text = {{reinterpret_cast<const uint8_t*>(contents.data()), contents.size()}, &contents};
        clippings_insert(&row);
      },
      1);
  check([&] { app_recent_missing(value); }, 1);
  check([&] { app_path_exists(value); }, 1);
  reads.clear();
  delete static_cast<BitmapReader*>(app_bitmap_open(value));
  assert(reads == std::vector<std::string>({"HOME", expected}));
  check([&] { app_header_draw({}, value); }, 1);
  check([&] { app_popup_draw(value); }, 1);
  check(
      [&] {
        XpAppText labels[4]{value, value, value, value};
        app_footer_draw(labels);
      },
      4);
  check(
      [&] {
        XpUiCommand command{};
        app_text_draw(&command, {}, value);
      },
      1);
  check([&] { app_text_width(value, 0, 0); }, 1);
  check([&] { app_text_wrap(value, {}, [](void*, XpAppText) -> uint8_t { return 1; }, nullptr); }, 1);
  check([&] { app_text_truncate(value, {}, [](void*, XpAppText) -> uint8_t { return 1; }, nullptr); }, 1);
  reads.clear();
  const auto height = app_text_box_height(value, {0, 20000, 3, 0});
  const auto actualReads = reads;
  reads.clear();
  const XpAppText parent{reinterpret_cast<const uint8_t*>(bytes.c_str()), bytes.size()};
  assert(app_text_box_height(parent, {0, 20000, 3, 0}) == height && reads == actualReads);
  // Length-aware conversions preserve payload after NUL and need no source terminator.
  std::vector<uint8_t> source(bytes.begin(), bytes.end());
  const XpAppText span{source.data(), source.size()};
  assert(owned(span) == bytes && app_owned(span) == bytes && view(span) == bytes);
}
}  // namespace BookTextAdapters
