idf_component_register(
    SRCS "app_map.cpp" "icon_map.c"
    INCLUDE_DIRS "."
    REQUIRES board_if espressif__libpng
    WHOLE_ARCHIVE
)

set_source_files_properties(
    app_map.cpp
    PROPERTIES
        COMPILE_FLAGS "-Wno-missing-field-initializers"
)
