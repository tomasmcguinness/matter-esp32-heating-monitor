idf_component_register(SRC_DIRS          "." "managers" "commands" "utilities"
                       INCLUDE_DIRS      "."
                       PRIV_INCLUDE_DIRS  "." "${ESP_MATTER_PATH}/examples/common/utils"
                       LDFRAGMENTS "linker.lf")

# Build the React app as part of `idf.py build` and embed the result in the firmware
# binary, so that an OTA update carries the UI with it.
#
# This is a custom *command* rather than a custom target so that its outputs have a
# producing rule: target_add_binary_data() lists each file as MAIN_DEPENDENCY, and ninja
# needs to know how to make them on a clean tree. It also keeps the build incremental --
# npm only runs when something under html_app/ has actually changed.
set(WEB_APP_DIR "${CMAKE_SOURCE_DIR}/html_compiled_app")
set(WEB_APP_FILES "${WEB_APP_DIR}/index.html" "${WEB_APP_DIR}/app.css" "${WEB_APP_DIR}/app.js")

file(GLOB_RECURSE WEB_APP_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_SOURCE_DIR}/html_app/src/*"
     "${CMAKE_SOURCE_DIR}/html_app/public/*")

add_custom_command(
    OUTPUT ${WEB_APP_FILES}
    COMMAND npm run build
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/html_app"
    DEPENDS ${WEB_APP_SOURCES}
            "${CMAKE_SOURCE_DIR}/html_app/index.html"
            "${CMAKE_SOURCE_DIR}/html_app/package.json"
            "${CMAKE_SOURCE_DIR}/html_app/vite.config.ts"
    COMMENT "Building React app..."
    VERBATIM)

add_custom_target(react_build DEPENDS ${WEB_APP_FILES})

foreach(web_app_file ${WEB_APP_FILES})
    target_add_binary_data(${COMPONENT_LIB} "${web_app_file}" BINARY)
endforeach()

if(CONFIG_SPIFFS_ATTESTATION_TRUST_STORE)
    spiffs_create_partition_image(paa_cert ${CMAKE_SOURCE_DIR}/paa_cert FLASH_IN_PROJECT)
endif()

target_compile_options(${COMPONENT_LIB} PRIVATE "-DCHIP_HAVE_CONFIG_H")
