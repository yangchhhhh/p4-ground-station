idf_component_register(
    SRCS "app_status.cpp" "icon_gy87.c"
    INCLUDE_DIRS "."
    REQUIRES board_if
    WHOLE_ARCHIVE
)

set_source_files_properties(
    app_status.cpp
    PROPERTIES
        COMPILE_FLAGS "-Wno-missing-field-initializers"
)
