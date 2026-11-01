# Local 2048 for brookesia_core 0.6.0 Phone App API.
# espressif/brookesia_app_game_2048 targets a newer core, so we vendor this.
idf_component_register(
    SRCS "app_game_2048.cpp" "icon_2048.c"
    INCLUDE_DIRS "."
    REQUIRES brookesia_core
    WHOLE_ARCHIVE
)

set_source_files_properties(
    app_game_2048.cpp
    PROPERTIES
        COMPILE_FLAGS "-Wno-missing-field-initializers"
)
