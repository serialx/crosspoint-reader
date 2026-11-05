#include <xp_ffi.h>

#include <cassert>
#include <cstring>
#include <memory>
#include <vector>

namespace {
// Deliberately unlike XpFontFile: stateful owners, extra fields, reordered data.
struct Deleter {
  size_t tag;
  void operator()(char* value) const { delete[] value; }
};
struct Owner {
  uint64_t guard = 0x13579;
  uint8_t style;
  std::unique_ptr<char[], Deleter> name;
  uint8_t size;
};
struct Context {
  std::vector<Owner> files;
  size_t reads = 0;
};
XpFontFile get(const void* raw, size_t index) {
  auto& ctx = *const_cast<Context*>(static_cast<const Context*>(raw));
  assert(index < ctx.files.size());
  const auto& file = ctx.files[index];
  assert(file.guard == 0x13579);
  ++ctx.reads;
  return {file.name.get(), file.size, file.style};
}
void add(Context& ctx, const char* name, uint8_t size, uint8_t style) {
  auto copy = std::unique_ptr<char[], Deleter>(new char[strlen(name) + 1], Deleter{42});
  strcpy(copy.get(), name);
  ctx.files.push_back({0x13579, style, std::move(copy), size});
}
}  // namespace
int main() {
  Context ctx;
  add(ctx, "Zebra.ttf", 255, 0);
  add(ctx, "Alpha.ttf", 0, 0);
  add(ctx, "Bold.ttf", 14, 1);
  add(ctx, "Italic.ttf", 14, 2);
  const XpFontFiles files{&ctx, ctx.files.size(), get};
  assert(sizeof(Owner) != sizeof(XpFontFile));
  assert(xp_font_file_nearest(&files, 255, 0, true) == 0);
  assert(xp_font_file_nearest(&files, 1, 0, false) == 1);
  assert(xp_font_file_nearest(&files, 14, 1, true) == 2);
  assert(xp_font_file_nearest(&files, 14, 3, false) == -1);
  assert(xp_font_has_size(&files, 0));
  assert(xp_font_has_size(&files, 255));
  assert(!xp_font_has_size(&files, 254));
  std::vector<uint8_t> sizes;
  xp_font_sizes(&files, &sizes,
                [](void* raw, uint8_t size) { static_cast<std::vector<uint8_t>*>(raw)->push_back(size); });
  assert((sizes == std::vector<uint8_t>{0, 14, 255}));
  // Non-identity candidate indices and equal weights force borrowed-name ties.
  const XpFontCandidate candidates[]{{3, 400, true}, {0, 400, false}, {2, 700, false}, {1, 400, false}};
  int32_t selected[4];
  assert(!xp_font_select_faces(candidates, 4, &files, selected));
  assert(selected[0] == 1 && selected[1] == 2 && selected[2] == 3 && selected[3] == -1);
  assert(ctx.reads > 0);
  const XpFontFiles empty{&ctx, 0, get};
  const size_t reads = ctx.reads;
  assert(xp_font_file_nearest(&empty, 0, 0, false) == -1);
  assert(!xp_font_has_size(&empty, 0));
  assert(!xp_font_select_faces(nullptr, 0, &empty, selected));
  assert(ctx.reads == reads);
  return 0;
}
