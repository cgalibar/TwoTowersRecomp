// Opt-in bring-up tracing for SLES-51256. Set TTR_TRACE to a comma-separated list of channels:
//   dma  - ClGfxDmaBuffer / VBlank DMA queue state
// TTR_FRAME_DUMP=<dir> saves the presented frame (and logs the ClGame state) every
// TTR_FRAME_DUMP_EVERY VBlanks (default 50, one second of PAL).
// Each traced guest function is wrapped: the wrapper logs, then runs the generated function.
#include "game/sles_51256_trace.h"

#include "debug/frame_dump.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "runtime/ps2_memory.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

#if TTR_HAS_GENERATED_CODE
#include <ps2_recompiled_functions.h>
#endif

namespace ttr::sles51256
{
    namespace
    {
        // Guest globals, relative to _gp = 0x003892F0.
        constexpr uint32_t kGp = 0x003892F0u;
        constexpr uint32_t kQueuedChains = kGp - 0x7BC0u;  // ClVBlankIntHandler pending chains
        constexpr uint32_t kQueueReadIndex = kGp - 0x7BBCu;
        constexpr uint32_t kFramesSinceKick = kGp - 0x7BB4u;
        constexpr uint32_t kWaitTwoFrames = kGp - 0x7BACu;  // u8
        constexpr uint32_t kVif1Buffer = kGp - 0x6F00u;    // ClGfxDmaBuffer* (channel 1, VIF1)
        constexpr uint32_t kGifBuffer = kGp - 0x6EFCu;     // ClGfxDmaBuffer* (channel 2, GIF)
        constexpr uint32_t kChainSlots = 0x0038DB10u;      // {vif1Chain, gifChain}[4]
        constexpr uint32_t kTheGame = 0x00382510u;         // ClGame {int mode; ClGameMode *current; ...}

        struct FrameDumpConfig
        {
            bool enabled = false;
            std::filesystem::path directory;
            uint32_t every = 50;
        };

        FrameDumpConfig &frameDumpConfig()
        {
            static FrameDumpConfig config = []
            {
                FrameDumpConfig c;
                if (const char *dir = std::getenv("TTR_FRAME_DUMP"))
                {
                    c.enabled = true;
                    c.directory = dir;
                    if (const char *every = std::getenv("TTR_FRAME_DUMP_EVERY"))
                    {
                        c.every = std::max(1, std::atoi(every));
                    }
                }
                return c;
            }();
            return config;
        }

        uint32_t ram32(const uint8_t *rdram, uint32_t address)
        {
            uint32_t value = 0;
            std::memcpy(&value, rdram + (address & 0x01FFFFFFu), sizeof(value));
            return value;
        }

        bool traceEnabled(std::string_view channel)
        {
            const char *env = std::getenv("TTR_TRACE");
            if (!env)
            {
                return false;
            }
            const std::string_view list{env};
            return list == "all" || list.find(channel) != std::string_view::npos;
        }

        void logBuffer(const uint8_t *rdram, const char *label, uint32_t pointerAddress)
        {
            const uint32_t buffer = ram32(rdram, pointerAddress);
            std::cout << ' ' << label << "@0x" << std::hex << buffer;
            if (buffer != 0u)
            {
                std::cout << "{sent=" << std::dec << ram32(rdram, buffer + 0x08u)
                          << " done=" << ram32(rdram, buffer + 0x10u)
                          << " sync=" << ram32(rdram, buffer + 0x14u)
                          << " flag=" << static_cast<int>(rdram[(buffer + 0x18u) & 0x01FFFFFFu]) << '}';
            }
            std::cout << std::dec;
        }

        void logDmaState(uint8_t *rdram, PS2Runtime *runtime, const char *event, R5900Context *ctx)
        {
            PS2Memory &memory = runtime->memory();
            const uint32_t readIndex = ram32(rdram, kQueueReadIndex) & 3u;
            std::cout << "[ttr:dma] " << event << " a0=0x" << std::hex << GPR_U32(ctx, 4)
                      << " a1=0x" << GPR_U32(ctx, 5)
                      << " slot[" << std::dec << readIndex << "]={0x" << std::hex
                      << ram32(rdram, kChainSlots + readIndex * 8u) << ",0x"
                      << ram32(rdram, kChainSlots + readIndex * 8u + 4u) << '}' << std::dec
                      << " frames=" << ram32(rdram, kFramesSinceKick)
                      << " wait2=" << static_cast<int>(rdram[kWaitTwoFrames & 0x01FFFFFFu])
                      << " queued=" << ram32(rdram, kQueuedChains)
                      << std::hex << " vif1.chcr=0x" << memory.readIORegister(0x10009000u)
                      << " gif.chcr=0x" << memory.readIORegister(0x1000A000u)
                      << " d_stat=0x" << memory.readIORegister(0x1000E010u) << std::dec;
            logBuffer(rdram, "vif1", kVif1Buffer);
            logBuffer(rdram, "gif", kGifBuffer);
            std::cout << std::endl;
        }

        // Rate limit: the first events, then one in every 64.
        bool shouldLog(uint32_t &counter)
        {
            ++counter;
            return counter <= 40u || (counter % 64u) == 0u;
        }

#if TTR_HAS_GENERATED_CODE
        void traceDmacHandler(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            static uint32_t count = 0;
            const bool log = shouldLog(count);
            if (log)
                logDmaState(rdram, runtime, "dmac-handler>", ctx);
            DmacInterruptHandler__14ClGfxDmaBufferFi_0x294650(rdram, ctx, runtime);
            if (log)
                logDmaState(rdram, runtime, "dmac-handler<", ctx);
        }

        void traceQueueDmaChain(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            static uint32_t count = 0;
            if (shouldLog(count))
                logDmaState(rdram, runtime, "queue-chain", ctx);
            QueueDmaChain__18ClVBlankIntHandlerFP1i_0x1064e0(rdram, ctx, runtime);
        }

        void dumpFrameIfDue(uint8_t *rdram, PS2Runtime *runtime)
        {
            static uint32_t vblanks = 0;
            const FrameDumpConfig &config = frameDumpConfig();
            if (!config.enabled || (++vblanks % config.every) != 0u)
            {
                return;
            }
            char name[32];
            std::snprintf(name, sizeof(name), "frame_%06u.png", vblanks);
            const bool saved = ttr::debug::dumpPresentedFrame(*runtime, config.directory / name);
            const uint32_t current = ram32(rdram, kTheGame + 4u);
            std::cout << "[ttr:frame] vblank=" << vblanks << ' ' << name << (saved ? "" : " (no frame)")
                      << " game.mode=" << ram32(rdram, kTheGame) << std::hex
                      << " game.current=0x" << current
                      << " vtbl=0x" << (current ? ram32(rdram, current) : 0u)
                      << " current.next=" << std::dec << (current ? ram32(rdram, current + 4u) : 0u)
                      << std::endl;
        }

        void traceVBlank(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            static uint32_t count = 0;
            static uint32_t pendingCount = 0;
            const bool dma = traceEnabled("dma");
            // Every frame while a chain is queued (first 60), otherwise rate limited.
            const bool pending = ram32(rdram, kQueuedChains) != 0u;
            const bool log = dma && (pending ? (++pendingCount <= 60u) : shouldLog(count));
            if (log)
                logDmaState(rdram, runtime, "vblank>", ctx);
            VBlankInterruptHandler__Fi_0x106290(rdram, ctx, runtime);
            if (log && pending)
                logDmaState(rdram, runtime, "vblank<", ctx);
            dumpFrameIfDue(rdram, runtime);
        }

        void traceDmaTimeout(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            logDmaState(rdram, runtime, "DMA-TIMEOUT", ctx);
            DmaTimeout__Fv_0x105a80(rdram, ctx, runtime);
        }
#endif
    }

    void installTracing(PS2Runtime &runtime)
    {
#if TTR_HAS_GENERATED_CODE
        if (traceEnabled("dma"))
        {
            runtime.replaceFunction(0x00294650u, &traceDmacHandler);
            runtime.replaceFunction(0x001064E0u, &traceQueueDmaChain);
            runtime.replaceFunction(0x00105A80u, &traceDmaTimeout);
            std::cout << "[ttr] DMA tracing enabled" << std::endl;
        }
        if (traceEnabled("dma") || frameDumpConfig().enabled)
        {
            runtime.replaceFunction(0x00106290u, &traceVBlank);
        }
        if (frameDumpConfig().enabled)
        {
            std::cout << "[ttr] dumping a frame every " << frameDumpConfig().every << " VBlanks to "
                      << frameDumpConfig().directory.string() << std::endl;
        }
#else
        (void)runtime;
#endif
    }
}
