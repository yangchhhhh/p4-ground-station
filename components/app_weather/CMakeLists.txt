idf_component_register(
    SRCS "app_weather.cpp" "icon_weather.c"
    INCLUDE_DIRS "."
    REQUIRES board_if esp_http_client cjson mbedtls esp-tls
    WHOLE_ARCHIVE
)

set_source_files_properties(
    app_weather.cpp
    PROPERTIES
        COMPILE_FLAGS "-Wno-missing-field-initializers"
)
