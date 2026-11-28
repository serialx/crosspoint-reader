#pragma once

#include <sys/wait.h>
#include <unistd.h>

#include <cassert>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <source_location>
#include <string>
#include <type_traits>
#include <utility>

namespace hal_contract {
inline void require(bool value, std::source_location at = std::source_location::current()) {
  if (!value) {
    std::fprintf(stderr, "HAL contract failed at %s:%u\n", at.file_name(), at.line());
    std::abort();
  }
}
// Each operation runs in a child so an expected abort cannot terminate the suite.
template <class Action>
void rejects(Action action, const char* diagnostic = "no impl") {
  std::fflush(nullptr);
  int errors[2];
  require(pipe(errors) == 0);
  const auto child = fork();
  require(child >= 0);
  if (!child) {
    close(errors[0]);
    require(dup2(errors[1], STDERR_FILENO) >= 0);
    close(errors[1]);
    action();
    _exit(0);
  }
  close(errors[1]);
  std::string output;
  char buffer[1024];
  for (;;) {
    const auto count = read(errors[0], buffer, sizeof(buffer));
    if (count < 0 && errno == EINTR) continue;
    require(count >= 0);
    if (!count) break;
    output.append(buffer, count);
  }
  close(errors[0]);
  int status = 0;
  require(waitpid(child, &status, 0) == child);
  require(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
  require(!diagnostic || output.find(diagnostic) != std::string::npos);
}

template <class File>
void safeQueries(File& file, bool open) {
  if constexpr (requires { file.isOpen(); }) require(bool(file.isOpen()) == open);
  if constexpr (requires { static_cast<bool>(file); }) require(static_cast<bool>(file) == open);
}

// Check every available overload, including aliases with their own early-return paths.
// Native fixture declarations also contain unimplemented production methods; its adapter
// exposes only the methods that this fixture defines.
template <class File>
void rejectsOperations(File& file) {
  uint8_t bytes[8]{};
  char name[128]{};
#define HAL_REJECT(expression)              \
  if constexpr (requires { expression; }) { \
    rejects([&] { (void)(expression); });   \
  }
  HAL_REJECT(file.close());
  HAL_REJECT(file.read());
  HAL_REJECT(file.read(bytes, sizeof(bytes)));
  HAL_REJECT(file.read(static_cast<void*>(bytes), sizeof(bytes)));
  HAL_REJECT(file.write(uint8_t{1}));
  HAL_REJECT(file.write(static_cast<const uint8_t*>(bytes), sizeof(bytes)));
  HAL_REJECT(file.write(static_cast<const void*>(bytes), sizeof(bytes)));
  HAL_REJECT(file.seek(0));
  HAL_REJECT(file.seekSet(0));
  HAL_REJECT(file.seekCur(0));
  HAL_REJECT(file.seekCur(-1));
  HAL_REJECT(file.seek64(0));
  HAL_REJECT(file.size());
  HAL_REJECT(file.fileSize());
  HAL_REJECT(file.fileSize64());
  HAL_REJECT(file.position());
  HAL_REJECT(file.available());
  HAL_REJECT(file.flush());
  HAL_REJECT(file.truncate(0));
  HAL_REJECT(file.isDirectory());
  HAL_REJECT(file.rewindDirectory());
  HAL_REJECT(file.getName(name, sizeof(name)));
  HAL_REJECT(file.rename("renamed"));
  HAL_REJECT(file.openNextFile());
#undef HAL_REJECT
}

template <class File>
void closeTwice(File& file) {
  if constexpr (requires { file.close(); }) {
    file.close();
    file.close();
    safeQueries(file, false);
  }
}

// open(file, path, success) drives the actual fixture's opening entry point. A family
// without an open-failure mechanism still tests empty-path and post-open cleanup.
template <class File, class Open>
void check(Open open, bool canFail = true) {
  static_assert(!std::is_copy_constructible_v<File>);
  static_assert(!std::is_copy_assignable_v<File>);
  static_assert(std::is_move_constructible_v<File>);
  static_assert(std::is_move_assignable_v<File>);
  File empty;
  safeQueries(empty, false);
  rejectsOperations(empty);
  if constexpr (requires { empty.modificationTime(); }) require(empty.modificationTime() == 0);
  for (const char* path : {"/missing", ""}) {
    File failed;
    open(failed, path, false);
    if (canFail) safeQueries(failed, false);
    closeTwice(failed);
  }
  File source;
  open(source, "contract-file", true);
  safeQueries(source, true);
  File moved(std::move(source));
  safeQueries(source, false);
  rejectsOperations(source);
  safeQueries(moved, true);
  File assigned;
  open(assigned, "contract-destination", true);
  assigned = std::move(moved);
  safeQueries(moved, false);
  rejectsOperations(moved);
  safeQueries(assigned, true);
  closeTwice(assigned);
  if constexpr (requires { empty.openNextFile(); }) {
    File directory;
    open(directory, "contract-directory", true);
    bool exhausted = false;
    for (unsigned i = 0; i < 1024; ++i) {
      auto entry = directory.openNextFile();
      exhausted = !static_cast<bool>(entry);
      closeTwice(entry);
      if (exhausted) break;
    }
    require(exhausted);
    closeTwice(directory);
  }
}
}  // namespace hal_contract
