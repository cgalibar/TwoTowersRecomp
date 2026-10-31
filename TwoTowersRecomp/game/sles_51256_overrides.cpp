// Per-build runtime overrides for The Two Towers, PAL Spain (SLES-51256, VER 1.00).
// Matched by ELF name, entry point and file CRC32 so they never leak into other builds.
#include "game/sles_51256.h"
#include "game_overrides.h"
#include "ps2_runtime.h"

namespace
{
    void applyTwoTowersSles51256(PS2Runtime &runtime)
    {
        // Address bindings go here as bring-up finds them, e.g.:
        //   ps2_game_overrides::bindAddressHandler(runtime, 0x00123456u, "sceCdRead");
        (void)runtime;
    }
}

PS2_REGISTER_GAME_OVERRIDE("The Two Towers (SLES-51256)", ttr::sles51256::kElfName, ttr::sles51256::kEntryPoint,
                           ttr::sles51256::kElfCrc32, &applyTwoTowersSles51256)
