add_executable(TwoTowersRecomp
    main.cpp
    bios/bios_rom.cpp
    debug/frame_dump.cpp
    debug/pad_script.cpp
    debug/sampling_profiler.cpp
    game/sles_51256_overrides.cpp
    game/sles_51256_trace.cpp
)

target_include_directories(TwoTowersRecomp PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(TwoTowersRecomp PRIVATE TwoTowersRecompLib ps2_runtime)
if(WIN32)
    target_link_libraries(TwoTowersRecomp PRIVATE dbghelp)
endif()

target_compile_definitions(TwoTowersRecomp PRIVATE
    TTR_VERSION="${PROJECT_VERSION}"
    TTR_BOOT_ELF_NAME="SLES_512.56"
)

if(TTR_ENABLE_DEBUG_UI)
    target_sources(TwoTowersRecomp PRIVATE "${TTR_PS2RECOMP_DIR}/ps2xRuntime/src/lib/ps2_debug_panel.cpp")
    target_link_libraries(TwoTowersRecomp PRIVATE rlImGui imgui)
    target_compile_definitions(TwoTowersRecomp PRIVATE PS2X_ENABLE_DEBUG_UI=1)
endif()

if(MSVC)
    # raylib's CloseWindow clashes with the WinAPI symbol on x64 (same workaround as ps2EntryRunner).
    target_link_options(TwoTowersRecomp PRIVATE "/FORCE:MULTIPLE")
    target_compile_options(TwoTowersRecomp PRIVATE /utf-8)
    # The debug switches are read with std::getenv on purpose.
    target_compile_definitions(TwoTowersRecomp PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# FFmpeg DLLs for the runtime's MPEG/IPU path.
if(COMMAND ps2x_stage_ffmpeg_runtime_dlls)
    ps2x_stage_ffmpeg_runtime_dlls(TwoTowersRecomp)
endif()

set_target_properties(TwoTowersRecomp PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:TwoTowersRecomp>")

# VU microprogram benchmark and fast-engine check on captured runs (PS2X_VU_CAPTURE); see docs/DEBUGGING.md.
add_executable(ttr_vu_bench debug/vu_bench.cpp)
target_link_libraries(ttr_vu_bench PRIVATE ps2_runtime)
if(MSVC)
    target_link_options(ttr_vu_bench PRIVATE "/FORCE:MULTIPLE")
    target_compile_options(ttr_vu_bench PRIVATE /utf-8)
    target_compile_definitions(ttr_vu_bench PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
