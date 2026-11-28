#include <HalStorage.h>

#include <cassert>

#include "util/PluginHttp.h"

namespace {
bool tracking = false;
size_t active = 0, maximum = 0;
}  // namespace

extern "C" __attribute__((no_instrument_function)) void __cyg_profile_func_enter(void*, void*) {
  if (tracking) {
    ++active;
    if (active > maximum) maximum = active;
  }
}
extern "C" __attribute__((no_instrument_function)) void __cyg_profile_func_exit(void*, void*) {
  if (tracking) --active;
}

size_t check(const std::string& path, const std::string& value, const std::string& expected) {
  const std::string file = "/plugins/demo/save.json";
  oracle::files.erase(file);
  active = maximum = 0;
  tracking = true;
  const bool saved = pluginhttp::saveTokenToFile(file, path, value);
  tracking = false;
  assert(saved == !expected.empty());
  assert(active == 0);
  if (saved) {
    const auto& bytes = oracle::files.at(file);
    if (std::string(bytes.begin(), bytes.end()) != expected)
      fprintf(stderr, "path bytes=%zu: got %.*s expected %s\n", path.size(), static_cast<int>(bytes.size()),
              bytes.data(), expected.c_str());
    assert(std::string(bytes.begin(), bytes.end()) == expected);
  } else {
    assert(!oracle::files.count(file));
  }
  return maximum;
}

int main() {
  oracle::dirs = {"/plugins", "/plugins/demo"};
  const auto shallow = check("x", "fresh", R"({"x":"fresh"})");
  check("", "fresh", "");
  check("...", "fresh", "");
  check("..a..b.", "q\"\n", R"({"a":{"b":"q\"\n"}})");
  check("0.01", "", R"({"0":{"01":""}})");
  check("q\".\\\n", "\b\f\r\t", R"({"q\"":{"\\\n":"\b\f\r\t"}})");
  check(std::string("a\0b.c", 5), std::string("v\0z", 3), R"({"a\u0000b":{"c":"v\u0000z"}})");
  check("한.é", "값", R"({"한":{"é":"값"}})");
  for (const size_t depth : {384, 512, 2048}) {
    std::string path = "x", expected;
    for (size_t i = 1; i < depth; ++i) path += ".x";
    for (size_t i = 0; i < depth; ++i) expected += R"({"x":)";
    expected += R"("fresh")";
    expected.append(depth, '}');
    const auto deep = check(path, "fresh", expected);
    printf("token path depth=%zu bytes=%zu call_depth=%zu shallow=%zu\n", depth, expected.size(), deep, shallow);
    fflush(stdout);
    // A recursive serializer fails this bound even on hosts with large stacks.
    assert(deep <= shallow + 4);
  }
}
