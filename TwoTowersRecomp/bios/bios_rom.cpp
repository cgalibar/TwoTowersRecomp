#include "bios/bios_rom.h"

#include "game/sles_51256.h"
#include "runtime/ps2_rom_device.h"

#include <cstring>
#include <fstream>
#include <iterator>
#include <string_view>

namespace ttr::bios
{
    namespace
    {
        constexpr size_t kBiosSize = 4u * 1024u * 1024u;
        constexpr size_t kRomdirEntrySize = 16u;

        void setError(std::string *error, std::string message)
        {
            if (error)
            {
                *error = std::move(message);
            }
        }

        uint32_t readLe32(const uint8_t *p)
        {
            return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                   (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
        }
    }

    std::optional<RomImage> loadRomImage(const std::filesystem::path &path, std::string *error)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            setError(error, "cannot open " + path.string());
            return std::nullopt;
        }
        const std::vector<uint8_t> data{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        if (data.size() != kBiosSize)
        {
            setError(error, path.string() + " is not a 4 MiB BIOS dump");
            return std::nullopt;
        }

        // ROMDIR: 16-byte entries {char name[10]; u16 extinfoSize; u32 fileSize}, starting with
        // RESET (whose data begins at offset 0). File data is laid out in order, 16-byte aligned.
        constexpr std::string_view kReset{"RESET\0", 6};
        size_t entry = 0;
        while (entry + kReset.size() <= data.size() &&
               std::memcmp(data.data() + entry, kReset.data(), kReset.size()) != 0)
        {
            ++entry;
        }
        if (entry + kReset.size() > data.size())
        {
            setError(error, path.string() + " has no ROMDIR");
            return std::nullopt;
        }

        RomImage image;
        image.path = path;
        size_t offset = 0;
        for (; entry + kRomdirEntrySize <= data.size(); entry += kRomdirEntrySize)
        {
            const char *name = reinterpret_cast<const char *>(data.data() + entry);
            const std::string fileName(name, strnlen(name, 10));
            if (fileName.empty())
            {
                break;
            }
            const uint32_t size = readLe32(data.data() + entry + 12);
            if (offset + size > data.size())
            {
                setError(error, path.string() + ": ROMDIR entry " + fileName + " is out of bounds");
                return std::nullopt;
            }
            image.files[fileName].assign(data.begin() + offset, data.begin() + offset + size);
            offset += (static_cast<size_t>(size) + 15u) & ~size_t{15u};
        }

        const auto romver = image.files.find("ROMVER");
        if (romver == image.files.end() || romver->second.size() < 14u)
        {
            setError(error, path.string() + " has no ROMVER");
            return std::nullopt;
        }
        image.romver.assign(romver->second.begin(), romver->second.begin() + 14);
        return image;
    }

    void registerRomProfile(const RomImage &image)
    {
        PS2RomProfile profile;
        profile.id = "console-bios-" + image.romver;
        profile.provider = "TwoTowersRecomp";
        profile.matcher.elfName = sles51256::kElfName;
        profile.matcher.entryPoint = sles51256::kEntryPoint;
        profile.matcher.crc32 = sles51256::kElfCrc32;
        // Same 14-byte layout as the runtime's built-in ROMVER.
        profile.files["ROMVER"].assign(image.romver.begin(), image.romver.end());
        PS2RomDevice::registerProfile(std::move(profile));
    }
}
