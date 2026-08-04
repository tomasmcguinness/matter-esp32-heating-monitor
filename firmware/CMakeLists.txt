# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED ENV{ESP_MATTER_PATH})
    message(FATAL_ERROR "Please set ESP_MATTER_PATH to the path of esp-matter repo")
endif(NOT DEFINED ENV{ESP_MATTER_PATH})

set(ESP_MATTER_PATH $ENV{ESP_MATTER_PATH})
set(MATTER_SDK_PATH ${ESP_MATTER_PATH}/connectedhomeip/connectedhomeip)

# CHIP external platform. We ship our own copy of the ESP32 platform layer in
# external_platform/ESP32_custom (it replaces the internal-EMAC ESPEthernetDriver::Init()
# with a W5500 one -- the ESP32-S3 has no EMAC). CONFIG_CHIP_EXTERNAL_PLATFORM_DIR is a GN
# label resolved against ${MATTER_SDK_PATH}/config/esp32, so pointing it straight at this
# repo would hard-code where esp-matter and this project sit on disk. Instead, sync our copy
# to the location esp-matter's own CI uses and keep the Kconfig path portable.
set(EXT_PLATFORM_DEST "${ESP_MATTER_PATH}/../platform/ESP32_custom")
file(MAKE_DIRECTORY "${EXT_PLATFORM_DEST}")
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/external_platform/ESP32_custom/" DESTINATION "${EXT_PLATFORM_DEST}")

# The copy above happens at configure time, so make CMake re-configure whenever one of
# those files changes -- otherwise an edit to the platform layer would be silently ignored
# until the next unrelated reconfigure.
file(GLOB_RECURSE EXT_PLATFORM_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/external_platform/ESP32_custom/*")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${EXT_PLATFORM_SRCS})

# This should be done before using the IDF_TARGET variable.
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(EXTRA_COMPONENT_DIRS
    "${MATTER_SDK_PATH}/config/esp32/components"
    "${ESP_MATTER_PATH}/components")

project(heating-monitor)

idf_build_set_property(CXX_COMPILE_OPTIONS "-std=gnu++17;-Os;-DCHIP_HAVE_CONFIG_H;-Wno-overloaded-virtual;-Wno-error=unused-result" APPEND)
idf_build_set_property(C_COMPILE_OPTIONS "-Os" APPEND)

# We don't need Thread Network Commissioning Driver
idf_build_set_property(COMPILE_OPTIONS "-D_NO_GENERIC_THREAD_NETWORK_COMMISSIONING_DRIVER_" APPEND)

# For RISCV chips, project_include.cmake sets -Wno-format, but does not clear various
# flags that depend on -Wformat
idf_build_set_property(COMPILE_OPTIONS "-Wno-format-nonliteral;-Wno-format-security;-Wformat=0" APPEND)
