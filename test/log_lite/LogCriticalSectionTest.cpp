#include <LogCriticalSection.h>

#include <cstdio>
#include <cstdlib>
#include <initializer_list>

static void require(bool condition) {
  if (condition) return;
  std::fputs("critical-section state was not restored\n", stderr);
  std::exit(1);
}

int main() {
#if configNUM_CORES == 1
  for (const UBaseType_t initialMask : {0u, 1u, 7u}) {
    interruptMask = initialMask;
    const auto initialRestores = restoreCount;
    {
      loglite::CriticalSection outer;
      require(interruptMask == 1);
      {
        loglite::CriticalSection inner;
        require(interruptMask == 1);
      }
      require(interruptMask == 1 && restoreCount == initialRestores + 1);
    }
    require(interruptMask == initialMask && restoreCount == initialRestores + 2);
  }
#else
  {
    loglite::CriticalSection outer;
    require(criticalDepth == 1);
    {
      loglite::CriticalSection inner;
      require(criticalDepth == 2);
    }
    require(criticalDepth == 1);
  }
  require(criticalDepth == 0 && restoreCount == 0);
#endif
}
