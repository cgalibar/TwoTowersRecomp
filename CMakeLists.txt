cmake_minimum_required(VERSION 3.28)

project(TwoTowersRecomp
    VERSION 0.0.1
    DESCRIPTION "Static recompilation of The Lord of the Rings: The Two Towers (PS2, SLES-51256)"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(TTR_PS2RECOMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tools/PS2Recomp" CACHE PATH
    "PS2Recomp checkout providing ps2xRuntime (git submodule)")
set(TTR_GENERATED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/TwoTowersRecompLib/generated" CACHE PATH
    "Recompiled guest sources produced by tools/recomp_codegen.py")

option(TTR_ENABLE_DEBUG_UI "Build the PS2Recomp ImGui debug panel into the executable" ON)
set(TTR_X64_ARCH "AVX" CACHE STRING "x64 SIMD baseline for game code (AVX or AVX2)")
set_property(CACHE TTR_X64_ARCH PROPERTY STRINGS AVX AVX2)

# Generated MMI/VU0 code uses SSE4.1+ intrinsics. MSVC accepts them without flags,
# clang-cl needs the target feature enabled, so both get an explicit baseline.
if(MSVC)
    add_compile_options("/arch:${TTR_X64_ARCH}")
endif()

if(NOT EXISTS "${TTR_PS2RECOMP_DIR}/ps2xRuntime/CMakeLists.txt")
    message(FATAL_ERROR
        "PS2Recomp was not found at '${TTR_PS2RECOMP_DIR}'.\n"
        "Run: git submodule update --init --recursive")
endif()

# Only the runtime is consumed by this build. The generator tools (ps2_recomp,
# ps2_analyzer) are a separate host build: see tools/build_tools.ps1.
set(PS2X_BUILD_RECOMP OFF CACHE BOOL "" FORCE)
set(PS2X_BUILD_ANALYZER OFF CACHE BOOL "" FORCE)
set(PS2X_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(PS2X_BUILD_STUDIO OFF CACHE BOOL "" FORCE)
set(PS2X_BUILD_RUNTIME ON CACHE BOOL "" FORCE)
set(PS2X_ENABLE_DEBUG_UI ${TTR_ENABLE_DEBUG_UI} CACHE BOOL "" FORCE)

add_subdirectory("${TTR_PS2RECOMP_DIR}" "${CMAKE_BINARY_DIR}/ps2recomp" EXCLUDE_FROM_ALL)

add_subdirectory(TwoTowersRecompLib)
add_subdirectory(TwoTowersRecomp)
