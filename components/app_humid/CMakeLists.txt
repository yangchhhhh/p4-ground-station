idf_component_register(
    SRCS "app_humid.cpp" "icon_humid.c"
    INCLUDE_DIRS "."
    REQUIRES board_if
    WHOLE_ARCHIVE
)

set_source_files_properties(
    app_humid.cpp
    PROPERTIES
        COMPILE_FLAGS "-Wno-missing-field-initializers"
)
