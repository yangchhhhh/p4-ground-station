idf_component_register(
    SRCS "app_files.cpp" "icon_files.c"
    INCLUDE_DIRS "."
    REQUIRES board_if
    WHOLE_ARCHIVE
)

set_source_files_properties(
    app_files.cpp
    PROPERTIES
        COMPILE_FLAGS "-Wno-missing-field-initializers"
)
