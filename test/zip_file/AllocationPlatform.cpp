// Compile the production leaves with fallible allocation probes, without firmware hooks.
#include <HalStorage.h>
#include <InflateStream.h>
#include <Logging.h>
#include <ZipFile.h>

#include <algorithm>
#include <cstring>
#include <optional>

#include "AllocationProbe.h"

#define malloc ziptest::allocate
#define makeUniqueNoThrow ziptest::unique
#include "../../lib/ZipFile/ZipPlatform.cpp"
#include "../../lib/miniz/src/InflateStream.cpp"
#undef makeUniqueNoThrow
#undef malloc
