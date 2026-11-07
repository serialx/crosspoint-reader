# ArduinoJson source for host oracles.
#
# The oracles compile against the release the firmware pins in platformio.ini
# (bblanchon/ArduinoJson 7.4.2). Fetching it into the build tree keeps the suite
# independent of a PlatformIO install: .pio/libdeps only exists after a firmware
# build, so it cannot be a test input. Set XP_TEST_ARDUINOJSON_SRC (or
# FETCHCONTENT_SOURCE_DIR_ARDUINOJSON) to an existing ArduinoJson "src"
# directory to reuse a checkout, for example offline.
if(XP_TEST_ARDUINOJSON_SRC)
  if(NOT EXISTS "${XP_TEST_ARDUINOJSON_SRC}/ArduinoJson.h")
    message(FATAL_ERROR
      "XP_TEST_ARDUINOJSON_SRC='${XP_TEST_ARDUINOJSON_SRC}' does not contain ArduinoJson.h. "
      "Point it at an ArduinoJson 7.4.2 'src' directory, or leave it unset to fetch the pin.")
  endif()
  return()
endif()

include(FetchContent)
set(XP_TEST_ARDUINOJSON_REPOSITORY "https://github.com/bblanchon/ArduinoJson.git" CACHE STRING
  "ArduinoJson repository for host oracles")
set(XP_TEST_ARDUINOJSON_COMMIT "733bc4ee82630c88c0a619a883cd3a206efae977" CACHE STRING
  "ArduinoJson commit for host oracles (v7.4.2, the platformio.ini pin)")
FetchContent_Declare(arduinojson
  GIT_REPOSITORY "${XP_TEST_ARDUINOJSON_REPOSITORY}"
  GIT_TAG "${XP_TEST_ARDUINOJSON_COMMIT}"
  GIT_SHALLOW FALSE)
FetchContent_MakeAvailable(arduinojson)
set(XP_TEST_ARDUINOJSON_SRC "${arduinojson_SOURCE_DIR}/src" CACHE PATH
  "ArduinoJson 'src' directory used by host oracles" FORCE)
if(NOT EXISTS "${XP_TEST_ARDUINOJSON_SRC}/ArduinoJson.h")
  message(FATAL_ERROR "ArduinoJson checkout at '${arduinojson_SOURCE_DIR}' has no src/ArduinoJson.h")
endif()
message(STATUS "ArduinoJson for host oracles: ${XP_TEST_ARDUINOJSON_SRC}")
