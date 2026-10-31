// Opt-in bring-up tracing for SLES-51256. Set TTR_TRACE to a comma-separated list of channels:
//   dma   - ClGfxDmaBuffer / VBlank DMA queue state
//   sound - ClMStream (EA MultiStream) loads and body-load state machine
//   rpc   - EE-side sceSifBindRpc / sceSifCallRpc / sceSifCheckStatRpc
// TTR_FRAME_DUMP=<dir> saves the presented frame (and logs the ClGame state) every
// TTR_FRAME_DUMP_EVERY VBlanks (default 50, one second of PAL).
// Each traced guest function is wrapped: the wrapper logs, then runs the generated function.
#include "game/sles_51256_trace.h"

#include "debug/frame_dump.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "runtime/ee_scheduler.h"
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

            static constexpr const char *kStatus[] = {"run", "ready", "wait", "wait-susp", "susp", "dormant"};
            static constexpr const char *kWait[] = {"-", "sleep", "sema", "evflag", "vsync", "external", "mpeg"};
            const EeKernelSnapshot snapshot = runtime->eeScheduler().snapshot();
            for (const EeThreadSnapshot &thread : snapshot.threads)
            {
                std::cout << "[ttr:thread] id=" << thread.id
                          << ' ' << kStatus[static_cast<int>(thread.status)]
                          << '/' << kWait[static_cast<int>(thread.waitReason)] << ':' << thread.waitId
                          << " prio=" << thread.currentPriority << std::hex
                          << " pc=0x" << thread.pc << " ra=0x" << thread.ra
                          << " entry=0x" << thread.entry << std::dec
                          << " depth=" << thread.invocationDepth << std::endl;
            }
            const ps2x::iop::DebugSnapshot iop = runtime->iopDebugSnapshot();
            std::cout << "[ttr:iop] cycles=" << iop.emulatorCycles << " instructions=" << iop.emulatorInstructions
                      << std::endl;
            for (const std::string &line : iop.diagnostics)
            {
                if (line.rfind("thread", 0) == 0)
                {
                    std::cout << "[ttr:iop] " << line << std::endl;
                }
            }
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

        // --- sound: ClMStream (EA MultiStream client) -------------------------------------
        std::string guestString(const uint8_t *rdram, uint32_t address, size_t maxLength = 96)
        {
            std::string text;
            for (size_t i = 0; i < maxLength && address != 0u; ++i)
            {
                const char c = static_cast<char>(rdram[(address + i) & 0x01FFFFFFu]);
                if (c == '\0')
                    break;
                text.push_back(c);
            }
            return text;
        }

        PS2Runtime *g_runtime = nullptr;

        // FUSTREAM.IRX (loaded at 0x86D00 in this boot order) keeps one 0x180-byte record per
        // stream at +0x17D00; byte +0x29 is its state (0 or 7 report "ready" to the EE).
        void logIopStream(uint32_t handle)
        {
            constexpr uint32_t kFustreamStreams = 0x00086D00u + 0x00017D00u;
            uint8_t record[0x60]{};
            if (!g_runtime || !g_runtime->readIopMemory(kFustreamStreams + handle * 0x180u, record, sizeof(record)))
            {
                return;
            }
            uint16_t field34 = 0;
            std::memcpy(&field34, record + 0x34, sizeof(field34));
            std::cout << " iopStream{state=" << static_cast<int>(record[0x29])
                      << " 2a=" << static_cast<int>(record[0x2A]) << " 2b=" << static_cast<int>(record[0x2B])
                      << " 2c=" << static_cast<int>(record[0x2C]) << " 34=" << field34 << " raw=";
            static constexpr char kHex[] = "0123456789abcdef";
            for (size_t i = 0; i < sizeof(record); ++i)
            {
                std::cout << kHex[record[i] >> 4] << kHex[record[i] & 15] << ((i % 16) == 15 ? "|" : "");
            }
            std::cout << '}';

            // FUSTREAM globals (reader/scheduler state polled by its VBlank thread).
            const auto dumpWords = [](uint32_t offset, size_t count)
            {
                uint32_t words[32]{};
                count = std::min<size_t>(count, 32u);
                if (!g_runtime->readIopMemory(0x00086D00u + offset, words, count * 4u))
                    return;
                std::cout << " +" << std::hex << offset << "{";
                for (size_t i = 0; i < count; ++i)
                    std::cout << words[i] << ' ';
                std::cout << std::dec << '}';
            };
            dumpWords(0x151B0u, 2);
            dumpWords(0x17180u, 32);
            dumpWords(0x174B0u, 12);
            dumpWords(0x17460u, 8);

            // Full records of streams 0, 8 and the bank slot 10, once per 500 calls.
            static uint32_t fullDumps = 0;
            if ((fullDumps++ % 40u) == 0u)
            {
                for (const uint32_t index : {0u, 8u, 10u})
                {
                    uint8_t full[0x180]{};
                    if (!g_runtime->readIopMemory(kFustreamStreams + index * 0x180u, full, sizeof(full)))
                        continue;
                    std::cout << "\n[ttr:iopstream] #" << index << ' ';
                    for (size_t i = 0; i < sizeof(full); ++i)
                        std::cout << kHex[full[i] >> 4] << kHex[full[i] & 15] << ((i % 16) == 15 ? " " : "");
                }
            }
        }

        void logStream(const uint8_t *rdram, const char *event, uint32_t stream)
        {
            std::cout << "[ttr:sound] " << event << " stream=0x" << std::hex << stream << std::dec;
            if (stream != 0u)
            {
                const int32_t handle = static_cast<int32_t>(ram32(rdram, stream + 0xBCu));
                std::cout << " name='" << guestString(rdram, stream + 4u, 48) << "'"
                          << " handle=" << handle
                          << " state=" << ram32(rdram, stream + 0xC4u)
                          << " waitInfo=" << static_cast<int>(rdram[(stream + 0xF1u) & 0x01FFFFFFu])
                          << " retries=" << ram32(rdram, stream + 0x1B4u);
                if (handle >= 0 && handle < 48)
                {
                    // Per-handle tables SOUND_GetStreamInfo copies (filled by SOUND_CopyIOPBuffer).
                    const uint32_t h = static_cast<uint32_t>(handle);
                    std::cout << " iop{st=" << static_cast<int>(rdram[0x004904C0u + h])
                              << " b4f0=" << static_cast<int>(rdram[0x004904F0u + h])
                              << " ready=" << static_cast<int>(rdram[0x00490550u + h])
                              << " b520=" << static_cast<int>(rdram[0x00490520u + h])
                              << " b580=" << static_cast<int>(rdram[0x00490580u + h])
                              << std::hex << " w400=0x" << ram32(rdram, 0x00490400u + h * 4u)
                              << " w610=0x" << ram32(rdram, 0x00490610u + h * 4u)
                              << " w790=0x" << ram32(rdram, 0x00490790u + h * 4u)
                              << " w850=0x" << ram32(rdram, 0x00490850u + h * 4u)
                              << " w910=0x" << ram32(rdram, 0x00490910u + h * 4u)
                              << " w9d0=0x" << ram32(rdram, 0x004909D0u + h * 4u) << std::dec << '}';
                    logIopStream(h);
                }
            }
            std::cout << std::endl;
        }

        void traceStreamLoad(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            std::cout << "[ttr:sound] load '" << guestString(rdram, GPR_U32(ctx, 5)) << "' type="
                      << GPR_U32(ctx, 9) << std::endl;
            const uint32_t stream = GPR_U32(ctx, 4);
            load__9ClMStreamFPCcbbbQ29ClMStream12ClStreamType_0x1f8ec0(rdram, ctx, runtime);
            logStream(rdram, "after-load", stream);
        }

        // --- rpc: EE side of SIF RPC ----------------------------------------------------------
        void traceBindRpc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            const uint32_t client = GPR_U32(ctx, 4);
            const uint32_t sid = GPR_U32(ctx, 5);
            const uint32_t mode = GPR_U32(ctx, 6);
            sceSifBindRpc_0x2ae558(rdram, ctx, runtime);
            std::cout << "[ttr:rpc] bind client=0x" << std::hex << client << " sid=0x" << sid << " mode=" << mode
                      << " -> " << std::dec << static_cast<int32_t>(GPR_U32(ctx, 2))
                      << " serve=0x" << std::hex << ram32(rdram, client + 0x24u) << std::dec << std::endl;
        }

        void traceCallRpc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            static uint32_t count = 0;
            const bool log = shouldLog(count);
            const uint32_t client = GPR_U32(ctx, 4);
            if (log)
            {
                std::cout << "[ttr:rpc] call#" << count << " client=0x" << std::hex << client
                          << " fno=0x" << GPR_U32(ctx, 5) << " mode=" << GPR_U32(ctx, 6)
                          << " send=0x" << GPR_U32(ctx, 7) << " ssize=0x" << GPR_U32(ctx, 8)
                          << " recv=0x" << GPR_U32(ctx, 9) << " rsize=0x" << GPR_U32(ctx, 10)
                          << " endfn=0x" << GPR_U32(ctx, 11) << std::dec << std::endl;
            }
            sceSifCallRpc_0x2ae728(rdram, ctx, runtime);
            if (log)
            {
                std::cout << "[ttr:rpc] call#" << count << " -> " << static_cast<int32_t>(GPR_U32(ctx, 2)) << std::endl;
            }
        }

        void traceCheckStatRpc(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            static uint32_t lastClient = 0;
            static int32_t lastResult = -12345;
            static uint32_t repeats = 0;
            const uint32_t client = GPR_U32(ctx, 4);
            sceSifCheckStatRpc_0x2ae918(rdram, ctx, runtime);
            const int32_t result = static_cast<int32_t>(GPR_U32(ctx, 2));
            if (client != lastClient || result != lastResult)
            {
                std::cout << "[ttr:rpc] checkstat client=0x" << std::hex << client << std::dec << " -> " << result
                          << " (previous repeated " << repeats << "x)" << std::endl;
                lastClient = client;
                lastResult = result;
                repeats = 0;
            }
            else
            {
                ++repeats;
            }
        }

        // Bring-up hack: report the bard (narrator) stream as loaded so loading screens that wait on
        // it can be passed while the SPU2 does not advance stream voices.
        void hackBardStreamLoaded(uint8_t *, R5900Context *ctx, PS2Runtime *)
        {
            SET_GPR_U32(ctx, 2, 1u);
            ctx->pc = GPR_U32(ctx, 31);
        }

        // EE side of the EA sound handshake: the IOP reply trailer and the globals it feeds.
        void logSoundHandshake(const uint8_t *rdram)
        {
            const uint32_t entries = ram32(rdram, 0x0048E400u);
            const uint32_t trailer = 0x0048E404u + std::min<uint32_t>(entries, 64u) * 0x20u;
            std::cout << "[ttr:sound] reply entries=" << entries << " trailer=" << std::hex;
            for (uint32_t i = 0; i < 12u; ++i)
                std::cout << ram32(rdram, trailer + i * 4u) << ' ';
            std::cout << " loadReq(gp-7DB8)=" << ram32(rdram, kGp - 0x7DB8u)
                      << " extStatus(gp-7544)=" << static_cast<int>(rdram[(kGp - 0x7544u) & 0x01FFFFFFu])
                      << " ext0(gp-757C)=" << ram32(rdram, kGp - 0x757Cu)
                      << " ext8(gp-7578)=" << ram32(rdram, kGp - 0x7578u)
                      << " ack(gp-75C4)=" << ram32(rdram, kGp - 0x75C4u)
                      << " req(gp-7574)=" << ram32(rdram, kGp - 0x7574u)
                      << " seq(gp-7580)=" << ram32(rdram, kGp - 0x7580u)
                      << " seqDone(gp-7610)=" << ram32(rdram, kGp - 0x7610u)
                      << " extCallback(gp-75C8)=" << ram32(rdram, kGp - 0x75C8u)
                      << " pending(gp-75A0)=" << ram32(rdram, kGp - 0x75A0u)
                      << " batch(gp-7524)=" << ram32(rdram, kGp - 0x7524u)
                      << " cmdFlag(gp-753C)=" << static_cast<int>(rdram[(kGp - 0x753Cu) & 0x01FFFFFFu])
                      << std::dec << std::endl;
        }

        void traceSoundTick(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            static uint32_t count = 0;
            if (shouldLog(count))
            {
                std::cout << "[ttr:sound] ClSound::tick #" << count << std::hex
                          << " seq=" << ram32(rdram, kGp - 0x7580u) << " seqDone=" << ram32(rdram, kGp - 0x7610u)
                          << " pending=" << ram32(rdram, kGp - 0x75A0u) << std::dec << std::endl;
            }
            tick__7ClSoundFv_0x1f5660(rdram, ctx, runtime);
        }

        void traceGetStatus(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            static uint32_t count = 0;
            if (shouldLog(count))
            {
                std::cout << "[ttr:sound] SOUND_GetStatus(" << static_cast<int32_t>(GPR_U32(ctx, 4)) << ", "
                          << static_cast<int32_t>(GPR_U32(ctx, 5)) << ") #" << count << std::endl;
            }
            SOUND_GetStatus__Fii_0x1fecf0(rdram, ctx, runtime);
        }

        void traceFlushIopCommand(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            static uint32_t count = 0;
            const bool log = shouldLog(count);
            const uint32_t pendingBefore = ram32(rdram, kGp - 0x75A0u);
            FlushIOPCommand__Fi_0x1fde60(rdram, ctx, runtime);
            if (log)
            {
                std::cout << "[ttr:sound] FlushIOPCommand #" << count << " pendingBefore=" << pendingBefore
                          << " -> " << static_cast<int32_t>(GPR_U32(ctx, 2)) << std::endl;
            }
        }

        void traceTickBodyLoad(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            static uint32_t count = 0;
            const uint32_t stream = GPR_U32(ctx, 4);
            const bool log = shouldLog(count);
            if (log)
                logSoundHandshake(rdram);
            if (log)
                logStream(rdram, "tickBodyLoad>", stream);
            tickBodyLoad__9ClMStreamFv_0x1f8150(rdram, ctx, runtime);
            if (log)
                logStream(rdram, "tickBodyLoad<", stream);
        }
#endif
    }

    void installTracing(PS2Runtime &runtime)
    {
#if TTR_HAS_GENERATED_CODE
        g_runtime = &runtime;
        if (traceEnabled("dma"))
        {
            runtime.replaceFunction(0x00294650u, &traceDmacHandler);
            runtime.replaceFunction(0x001064E0u, &traceQueueDmaChain);
            runtime.replaceFunction(0x00105A80u, &traceDmaTimeout);
            std::cout << "[ttr] DMA tracing enabled" << std::endl;
        }
        if (traceEnabled("sound"))
        {
            runtime.replaceFunction(0x001F8EC0u, &traceStreamLoad);
            runtime.replaceFunction(0x001F8150u, &traceTickBodyLoad);
            runtime.replaceFunction(0x001F5660u, &traceSoundTick);
            runtime.replaceFunction(0x001FECF0u, &traceGetStatus);
            runtime.replaceFunction(0x001FDE60u, &traceFlushIopCommand);
            std::cout << "[ttr] sound tracing enabled" << std::endl;
        }
        if (const char *hack = std::getenv("TTR_HACK_SKIP_BARD"); hack && hack[0] == '1')
        {
            runtime.replaceFunction(0x001F14B0u, &hackBardStreamLoaded);
            std::cout << "[ttr] hack: isBardStreamLoaded() always true" << std::endl;
        }
        if (traceEnabled("rpc"))
        {
            runtime.replaceFunction(0x002AE558u, &traceBindRpc);
            runtime.replaceFunction(0x002AE728u, &traceCallRpc);
            runtime.replaceFunction(0x002AE918u, &traceCheckStatRpc);
            std::cout << "[ttr] SIF RPC tracing enabled" << std::endl;
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
