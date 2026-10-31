// Reads files out of a user-supplied PS2 BIOS dump (rom0 ROMDIR).
// The runtime is a no-BIOS HLE environment; only selected rom0 files are taken from the dump.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ttr::bios
{
    struct RomImage
    {
        std::filesystem::path path;
        std::string romver; // e.g. "0160EC20011004"
        std::unordered_map<std::string, std::vector<uint8_t>> files;
    };

    // Parses a 4 MiB dump. Returns std::nullopt (and fills *error) when it is not a PS2 BIOS.
    std::optional<RomImage> loadRomImage(const std::filesystem::path &path, std::string *error = nullptr);

    // Registers a rom0 profile for the supported game build that serves ROMVER from the dump,
    // replacing the runtime's built-in US v2.00 value.
    void registerRomProfile(const RomImage &image);
}
