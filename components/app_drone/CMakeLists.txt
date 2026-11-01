idf_component_register(
    SRCS "app_drone.cpp" "icon_drone.c"
    INCLUDE_DIRS "."
    REQUIRES board_if
    WHOLE_ARCHIVE
)

set_source_files_properties(
    app_drone.cpp
    PROPERTIES
        COMPILE_FLAGS "-Wno-missing-field-initializers"
)
