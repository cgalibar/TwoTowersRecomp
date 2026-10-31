// TwoTowersRecomp entry point.
// Based on ps2xRuntime/src/main.cpp from PS2Recomp (GPL-3.0).
#include "bios/bios_rom.h"
#include "ps2_runtime.h"
#if defined(PS2X_ENABLE_DEBUG_UI)
#include "ps2_debug_panel.h"
#endif

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace
{
    constexpr std::string_view kBootElf = TTR_BOOT_ELF_NAME;
    constexpr const char *kWindowTitle = "El Senor de los Anillos: Las Dos Torres - Recompiled";

    void setupTerminateLogger()
    {
        std::set_terminate([]()
                           {
                               std::cerr << "[terminate] unhandled exception" << std::endl;
                               if (const std::exception_ptr ep = std::current_exception())
                               {
                                   try
                                   {
                                       std::rethrow_exception(ep);
                                   }
                                   catch (const std::exception &e)
                                   {
                                       std::cerr << "[terminate] " << e.what() << std::endl;
                                   }
                                   catch (...)
                                   {
                                       std::cerr << "[terminate] non-std exception" << std::endl;
                                   }
                               }
                               std::abort(); });
    }

    fs::path executableDirectory(const char *argv0)
    {
#if defined(_WIN32)
        wchar_t buffer[MAX_PATH * 4];
        const DWORD length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
        if (length > 0 && length < std::size(buffer))
        {
            return fs::path(buffer).parent_path();
        }
#endif
        std::error_code ec;
        return fs::absolute(fs::path(argv0), ec).parent_path();
    }

    // Accepts either the boot ELF itself or a folder holding the extracted disc.
    std::optional<fs::path> resolveBootElf(const fs::path &candidate)
    {
        std::error_code ec;
        if (fs::is_regular_file(candidate, ec))
        {
            return candidate;
        }
        const fs::path elf = candidate / kBootElf;
        if (fs::is_regular_file(elf, ec))
        {
            return elf;
        }
        return std::nullopt;
    }

    // Accepts a BIOS dump or a folder holding one (first *.bin by name).
    std::optional<fs::path> resolveBios(const fs::path &candidate)
    {
        std::error_code ec;
        if (fs::is_regular_file(candidate, ec))
        {
            return candidate;
        }
        std::vector<fs::path> dumps;
        for (const fs::directory_entry &entry : fs::directory_iterator(candidate, ec))
        {
            std::string ext = entry.path().extension().string();
            std::ranges::transform(ext, ext.begin(), [](unsigned char c)
                                   { return static_cast<char>(std::tolower(c)); });
            if (ext == ".bin" && entry.is_regular_file(ec))
            {
                dumps.push_back(entry.path());
            }
        }
        if (dumps.empty())
        {
            return std::nullopt;
        }
        std::ranges::sort(dumps);
        return dumps.front();
    }

    using Resolver = std::optional<fs::path> (*)(const fs::path &);

    // Search order: <flag> <path>, <exe>/<exeFolder>/, then TwoTowersRecompLib/private/<privateFolder>/
    // found by walking up from the executable (development builds).
    std::optional<fs::path> findUserData(int argc, char *argv[], std::string_view flag, const char *exeFolder,
                                         const char *privateFolder, Resolver resolve)
    {
        for (int i = 1; i < argc; ++i)
        {
            if (argv[i] == flag && i + 1 < argc)
            {
                return resolve(fs::path(argv[i + 1]));
            }
        }

        const fs::path exeDir = executableDirectory(argv[0]);
        if (auto found = resolve(exeDir / exeFolder))
        {
            return found;
        }

        for (fs::path dir = exeDir; !dir.empty(); dir = dir.parent_path())
        {
            if (auto found = resolve(dir / "TwoTowersRecompLib" / "private" / privateFolder))
            {
                return found;
            }
            if (dir == dir.root_path())
            {
                break;
            }
        }
        return std::nullopt;
    }

    // The BIOS is optional: without it the runtime keeps its built-in rom0 contents.
    void configureBios(int argc, char *argv[])
    {
        const std::optional<fs::path> biosPath = findUserData(argc, argv, "--bios", "bios", "bios", &resolveBios);
        if (!biosPath)
        {
            std::cout << "No BIOS dump found; using the runtime's built-in ROMVER (install one with tools/bios_info.py)."
                      << std::endl;
            return;
        }
        std::string error;
        const std::optional<ttr::bios::RomImage> image = ttr::bios::loadRomImage(*biosPath, &error);
        if (!image)
        {
            std::cerr << "Ignoring BIOS dump: " << error << std::endl;
            return;
        }
        ttr::bios::registerRomProfile(*image);
        std::cout << "BIOS " << biosPath->filename().string() << " ROMVER " << image->romver << std::endl;
    }
}

int main(int argc, char *argv[])
{
    setupTerminateLogger();

    try
    {
        const std::optional<fs::path> bootElf = findUserData(argc, argv, "--game", "game", "disc", &resolveBootElf);
        if (!bootElf)
        {
            std::cerr << "Could not find " << kBootElf << ".\n"
                      << "Extract your disc with tools/extract_iso.py, place it in a 'game' folder next to the\n"
                      << "executable, or pass --game <folder-or-elf>." << std::endl;
            return 1;
        }
        std::cout << "TwoTowersRecomp " << TTR_VERSION << " booting " << bootElf->string() << std::endl;
#if !TTR_HAS_GENERATED_CODE
        std::cout << "Warning: built without generated guest code; the function table is empty." << std::endl;
#endif
        configureBios(argc, argv);

        PS2Runtime runtime;
#if defined(PS2X_ENABLE_DEBUG_UI)
        PS2DebugPanel debugPanel;
        runtime.setDebugUiCallbacks(
            [](PS2Runtime &, void *userData)
            { static_cast<PS2DebugPanel *>(userData)->initialize(); },
            [](PS2Runtime &rt, void *userData)
            { static_cast<PS2DebugPanel *>(userData)->draw(rt); },
            [](PS2Runtime &, void *userData)
            { static_cast<PS2DebugPanel *>(userData)->shutdown(); },
            &debugPanel);
#endif
        if (!runtime.initialize(kWindowTitle))
        {
            std::cerr << "Failed to initialize PS2 runtime" << std::endl;
            return 1;
        }

        if (!runtime.loadELF(bootElf->string()))
        {
            std::cerr << "Failed to load ELF file: " << bootElf->string() << std::endl;
            return 1;
        }

        runtime.run();

        std::cout.flush();
        std::cerr.flush();
        std::_Exit(0);
    }
    catch (const std::exception &e)
    {
        std::cerr << "[main] fatal exception: " << e.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << "[main] fatal exception: unknown" << std::endl;
    }

    std::cout.flush();
    std::cerr.flush();
    std::_Exit(1);
}
