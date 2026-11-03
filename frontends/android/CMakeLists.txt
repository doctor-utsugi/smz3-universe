cmake_minimum_required(VERSION 3.22)
project(smz3core C)

add_library(smz3core SHARED
    jni/smz3_jni.c
    ../../core/smz3_state.c
    ../../core/smz3_inventory.c
    ../../core/smz3_locations.c
    ../../core/gen/smz3_generate.c
    ../../core/gen/smz3_rom.c
    ../../core/gen/smz3_patch.c
    ../../core/gen/smz3_text.c
    ../../core/gen/smz3_hints.c
    ../../core/gen/smz3_itemopts.c
    ../../core/gen/smz3_plando.c
    ../../core/gen/smz3_fill.c
    ../../core/gen/smz3_world.c
    ../../core/gen/smz3_world_gen.c
    ../../core/gen/smz3_rng.c
    ../../core/gen/smz3_sprite.c
    ../../core/gen/smz3_goalicons.c
    ../../core/gen/smz3_romderived.c
    ../../core/gen/smz3_dungeonmaps.c
    ../../core/ui/smz3_ui.c
    ../../core/ui/smz3_ui_opts.c
    ../../core/ui/smz3_run.c
    ../../core/ui/smz3_host.c
    ../../core/ui/smz3_goal.c
    ../../core/ui/smz3_daily.c
    ../../core/ui/smz3_shortcuts.c
    ../../core/ui/smz3_counters.c
    ../../core/ui/smz3_runlogic.c
    ../../core/ui/smz3_tracker.c
    ../../core/ui/smz3_rewind.c)
target_include_directories(smz3core PRIVATE ../../core ../../core/gen ../../core/ui)
target_compile_options(smz3core PRIVATE -O2 -Wno-parentheses -Wno-unused-function)
target_link_options(smz3core PRIVATE -Wl,-z,max-page-size=16384)
