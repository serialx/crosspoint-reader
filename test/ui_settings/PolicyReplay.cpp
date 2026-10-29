#include <xp_ui_settings.h>

#include <cstdio>

int main() {
  unsigned kind, value, count, minimum, maximum, step, operation, expected;
  while (std::scanf("%u %u %u %u %u %u %u %u", &kind, &value, &count, &minimum, &maximum, &step, &operation,
                    &expected) == 8) {
    const auto result = xp_ui_settings_edit(kind, value, count, minimum, maximum, step);
    if (result.kind != operation || result.value != static_cast<int32_t>(expected)) {
      std::fprintf(stderr, "edit mismatch: kind=%u value=%u count=%u range=%u,%u,%u actual=%u,%d expected=%u,%u\n",
                   kind, value, count, minimum, maximum, step, result.kind, result.value, operation, expected);
      return 1;
    }
  }
  return std::feof(stdin) ? 0 : 2;
}
