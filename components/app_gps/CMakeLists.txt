idf_component_register(
    SRCS "app_gps.cpp" "icon_gps.c"
    INCLUDE_DIRS "."
    REQUIRES board_if
    WHOLE_ARCHIVE
)

set_source_files_properties(
    app_gps.cpp
    PROPERTIES
        COMPILE_FLAGS "-Wno-missing-field-initializers"
)
