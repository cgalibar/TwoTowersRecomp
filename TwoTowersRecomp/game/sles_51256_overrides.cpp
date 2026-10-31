// Per-build runtime overrides for The Two Towers, PAL Spain (SLES-51256, VER 1.00).
// Matched by ELF name, entry point and file CRC32 so they never leak into other builds.
#include "game_overrides.h"
#include "ps2_runtime.h"

namespace
{
    constexpr uint32_t kEntryPoint = 0x00100008u;
    constexpr uint32_t kElfCrc32 = 0x6BDD1874u;

    void applyTwoTowersSles51256(PS2Runtime &runtime)
    {
        // Address bindings go here as bring-up finds them, e.g.:
        //   ps2_game_overrides::bindAddressHandler(runtime, 0x00123456u, "sceCdRead");
        (void)runtime;
    }
}

PS2_REGISTER_GAME_OVERRIDE("The Two Towers (SLES-51256)", "SLES_512.56", kEntryPoint, kElfCrc32, &applyTwoTowersSles51256)
