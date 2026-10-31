# Recompiled EE code for SLES_512.56. The sources are generated locally from the
# user's own disc and are never committed.

file(GLOB TTR_GENERATED_SOURCES CONFIGURE_DEPENDS "${TTR_GENERATED_DIR}/*.cpp")

if(TTR_GENERATED_SOURCES)
    list(LENGTH TTR_GENERATED_SOURCES TTR_GENERATED_COUNT)
    message(STATUS "TwoTowersRecompLib: ${TTR_GENERATED_COUNT} generated sources in ${TTR_GENERATED_DIR}")
    add_library(TwoTowersRecompLib OBJECT ${TTR_GENERATED_SOURCES})
    target_include_directories(TwoTowersRecompLib PUBLIC "${TTR_GENERATED_DIR}")
    target_compile_definitions(TwoTowersRecompLib PUBLIC TTR_HAS_GENERATED_CODE=1)
    # Thousands of small translation units: batch them to keep build times sane.
    set_target_properties(TwoTowersRecompLib PROPERTIES
        UNITY_BUILD ON
        UNITY_BUILD_BATCH_SIZE 64)
else()
    message(WARNING
        "TwoTowersRecompLib: no generated sources in '${TTR_GENERATED_DIR}'. "
        "Building with an empty function table; run tools/recomp_codegen.py first to get a bootable build.")
    add_library(TwoTowersRecompLib OBJECT placeholder/register_functions.cpp)
    target_compile_definitions(TwoTowersRecompLib PUBLIC TTR_HAS_GENERATED_CODE=0)
endif()

target_link_libraries(TwoTowersRecompLib PUBLIC ps2_runtime)

if(MSVC)
    # Generated functions are huge switch-free straight-line blocks.
    target_compile_options(TwoTowersRecompLib PRIVATE /bigobj)
endif()
