# FreeInk SDK as an ESP-IDF component (no arduino-esp32).
#
# Builds the display (FreeInkDisplay + BoardConfig) and FreeInkUI on plain
# ESP-IDF, with idf/ supplying the slice of the Arduino API they call. Arduino
# and PlatformIO builds never read this file: they pull libs/* through lib_deps.
# See docs/esp-idf.md.
#
# Pick the device(s) in the project's top-level CMakeLists.txt, before
# project(), for example:
#   idf_build_set_property(COMPILE_DEFINITIONS "FREEINK_DEVICE_STICKY=1" APPEND)

file(GLOB _display_srcs
     "${CMAKE_CURRENT_LIST_DIR}/libs/display/FreeInkDisplay/src/*.cpp"
     "${CMAKE_CURRENT_LIST_DIR}/libs/display/FreeInkDisplay/src/bus/*.cpp"
     "${CMAKE_CURRENT_LIST_DIR}/libs/display/FreeInkDisplay/src/driver/*.cpp")

idf_component_register(
  SRCS ${_display_srcs}
       "libs/ui/FreeInkUI/src/FreeInkUI.cpp"
       "idf/src/ArduinoCompat.cpp"
  INCLUDE_DIRS "idf/include"
               "libs/hardware/BoardConfig/include"
               "libs/display/FreeInkDisplay/include"
               "libs/ui/FreeInkUI/include"
  PRIV_INCLUDE_DIRS "libs/display/FreeInkDisplay/src"
  REQUIRES esp_driver_gpio esp_driver_spi esp_timer
  PRIV_REQUIRES esp_pm)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-variable -Wno-unused-function)
