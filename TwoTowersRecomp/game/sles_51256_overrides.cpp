// Per-build runtime overrides for The Two Towers, PAL Spain (SLES-51256, VER 1.00).
// Matched by ELF name, entry point and file CRC32 so they never leak into other builds.
#include "game/sles_51256.h"
#include "game/sles_51256_trace.h"
#include "game_overrides.h"
#include "ps2_runtime.h"

namespace
{
    // ClMemory::initialize() hands the game all RAM from _end (0x589280) up to 32 MiB and makes the
    // permanent allocations (DMA buffers, FMV memory) from the top: exactly where the runtime keeps
    // its RPC/TLS descriptors and callback stacks by default. The game overwrote a server descriptor
    // there and sceSifCallRpc then copied 8 KiB of EA sound commands over the small-data globals.
    // Nothing is loaded between the kernel and the ELF (0x80000-0x100000): the runtime goes there.
    constexpr uint32_t kRuntimeRegionBase = 0x00080000u;  // descriptors + callback stacks
    constexpr uint32_t kRuntimeRegionSize = 0x00060000u;
    constexpr uint32_t kGuestHeapBase = 0x000E0000u;      // libc malloc (stdio, dtoa) and runtime stubs
    constexpr uint32_t kGuestHeapLimit = 0x00100000u;

    void applyTwoTowersSles51256(PS2Runtime &runtime)
    {
        runtime.setRuntimeGuestRegion(kRuntimeRegionBase, kRuntimeRegionSize);
        runtime.configureGuestHeap(kGuestHeapBase, kGuestHeapLimit);
        ttr::sles51256::installTracing(runtime);
    }
}

PS2_REGISTER_GAME_OVERRIDE("The Two Towers (SLES-51256)", ttr::sles51256::kElfName, ttr::sles51256::kEntryPoint,
                           ttr::sles51256::kElfCrc32, &applyTwoTowersSles51256)
