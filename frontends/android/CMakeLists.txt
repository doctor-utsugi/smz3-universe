cmake_minimum_required(VERSION 3.22)
project(smz3core C)

# every A Link to the Past generator source, like the 3DS build copies them
file(GLOB Z3GEN_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../../core/z3gen/*.c)
add_library(smz3core SHARED
    jni/smz3_jni.c
    ../../core/smz3_state.c
    ../../core/smz3_inventory.c
    ../../core/smz3_locations.c
    ../../core/state/smz3_statecvt.c
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
    ../../core/gen/smz3_md5.c
    ../../core/ui/smz3_ui.c
    ../../core/ui/smz3_ui_opts.c
    ../../core/ui/smz3_run.c
    ../../core/ui/smz3_host.c
    ../../core/ui/smz3_goal.c
    ../../core/ui/smz3_daily.c ../../core/ui/smz3_race.c
    ../../core/ui/smz3_shortcuts.c
    ../../core/ui/smz3_counters.c
    ../../core/ui/smz3_runlogic.c
    ../../core/ui/smz3_tracker.c
    ../../core/ui/smz3_smmap.c
    ../../core/ui/smz3_json.c ../../core/ui/smz3_romm.c ../../core/ui/smz3_sync.c
    ../../core/ui/smz3_rewind.c
    ../../core/ui/smz3_sm.c
    ../../core/smgen/sm_generate.c
    ../../core/smgen/sm_fill.c
    ../../core/smgen/sm_world.c
    ../../core/smgen/sm_world_gen.c
    ../../core/smgen/sm_patch.c
    ../../core/smgen/sm_options_gen.c
    ${CMAKE_CURRENT_BINARY_DIR}/sm_ips_data.c
    ../../core/ui/smz3_z3.c
    ${Z3GEN_SRC}
    ${CMAKE_CURRENT_BINARY_DIR}/z3_ips_data.c)
target_include_directories(smz3core PRIVATE ../../core ../../core/gen ../../core/smgen ../../core/z3gen ../../core/ui)
# the Super Metroid base patch as a C array, made at build time like the PC build does
find_program(PYTHON3 python3 REQUIRED)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/sm_ips_data.c
    COMMAND ${PYTHON3} ${CMAKE_CURRENT_SOURCE_DIR}/../../tools/gen_sm_ips_c.py ${CMAKE_CURRENT_SOURCE_DIR}/../../patches/sm_randomizer.ips ${CMAKE_CURRENT_BINARY_DIR}/sm_ips_data.c
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../../tools/gen_sm_ips_c.py ${CMAKE_CURRENT_SOURCE_DIR}/../../patches/sm_randomizer.ips)
# and A Link to the Past's (z3randomizer)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/z3_ips_data.c
    COMMAND ${PYTHON3} ${CMAKE_CURRENT_SOURCE_DIR}/../../tools/z3/gen_z3_ips_c.py ${CMAKE_CURRENT_SOURCE_DIR}/../../patches/z3_base.ips ${CMAKE_CURRENT_BINARY_DIR}/z3_ips_data.c
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../../tools/z3/gen_z3_ips_c.py ${CMAKE_CURRENT_SOURCE_DIR}/../../patches/z3_base.ips)
# the release tag shown on the second screen: the tag CI builds, else the newest tag here
if("$ENV{GITHUB_REF_NAME}" MATCHES "^v")
    set(SMZ3_VERSION "$ENV{GITHUB_REF_NAME}")
else()
    execute_process(COMMAND git describe --tags --abbrev=0 WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE SMZ3_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(NOT SMZ3_VERSION)
        set(SMZ3_VERSION dev)
    endif()
endif()
# configure again when a tag is added, or a cached build keeps the old version (and offers itself the current release)
execute_process(COMMAND git rev-parse --absolute-git-dir WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE SMZ3_GIT_DIR OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
foreach(f HEAD packed-refs refs/tags)
    if(SMZ3_GIT_DIR AND EXISTS "${SMZ3_GIT_DIR}/${f}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SMZ3_GIT_DIR}/${f}")
    endif()
endforeach()
target_compile_definitions(smz3core PRIVATE SMZ3_VERSION="${SMZ3_VERSION}")
target_compile_options(smz3core PRIVATE -O2 -Wno-parentheses -Wno-unused-function)
target_link_options(smz3core PRIVATE -Wl,-z,max-page-size=16384)
# Map Rando (core/maprando, Rust): linked in when its sources are fetched (tools/maprando/fetch.sh) and cargo-ndk is installed;
# the crate's rust-toolchain.toml pins the compiler, so cargo runs from the crate's folder
set(MR_CRATE ${CMAKE_CURRENT_SOURCE_DIR}/../../core/maprando)
find_program(CARGO cargo HINTS $ENV{HOME}/.cargo/bin)
if(CARGO AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../../vendor/maprando/rust/maprando AND ANDROID_ABI STREQUAL "arm64-v8a")
    set(MR_TARGET ${CMAKE_CURRENT_SOURCE_DIR}/../../build/maprando-target)
    set(MR_LIB ${MR_TARGET}/aarch64-linux-android/release/libsmz3_maprando.a)
    add_custom_target(maprando_crate
        COMMAND ${CMAKE_COMMAND} -E env ANDROID_NDK_HOME=${ANDROID_NDK} ${CARGO} ndk -t arm64-v8a -P 23 build --release --lib --target-dir ${MR_TARGET}
        WORKING_DIRECTORY ${MR_CRATE} BYPRODUCTS ${MR_LIB} VERBATIM)
    add_dependencies(smz3core maprando_crate)
    target_compile_definitions(smz3core PRIVATE SMZ_MAPRANDO)
    target_include_directories(smz3core PRIVATE ${MR_CRATE})
    target_link_libraries(smz3core PRIVATE ${MR_LIB} log dl m)
endif()
