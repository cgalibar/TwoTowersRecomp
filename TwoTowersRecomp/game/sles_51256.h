// Identity of the supported build: The Two Towers, PAL Spain (SLES-51256, VER 1.00).
#pragma once

#include <cstdint>

namespace ttr::sles51256
{
    inline constexpr const char *kElfName = "SLES_512.56";
    inline constexpr uint32_t kEntryPoint = 0x00100008u;
    inline constexpr uint32_t kElfCrc32 = 0x6BDD1874u;
}
