// Opt-in bring-up tracing for SLES-51256. Set TTR_TRACE to a comma-separated list of channels:
//   dma   - ClGfxDmaBuffer / VBlank DMA queue state
//   sound - ClMStream (EA MultiStream) loads and body-load state machine
//   rpc   - EE-side sceSifBindRpc / sceSifCallRpc / sceSifCheckStatRpc
//   fmv   - ClFMV playback flags, nodata callback and FMV audio requests
//   camera - ClCamera projection/Z values, world draw passes and a C++ check of SfMatrixMultiplyVu0
//   pad    - ClPad slot 0: libpad status, terminal mode, buttons and sticks as the game sees them
//   anims  - calls of the animated-character passes (ClDisplayList) and Noam figures, with each frame dump
// TTR_GS_DUMP=<vblank> prints the GS debug history (registers and primitives) for 10 VBlanks.
// TTR_FRAME_DUMP=<dir> saves the presented frame (and logs the ClGame state) every
// TTR_FRAME_DUMP_EVERY VBlanks (default 50, one second of PAL).
// Each traced guest function is wrapped: the wrapper logs, then runs the generated function.
#include "game/sles_51256_trace.h"

#include "debug/frame_dump.h"
#include "debug/pad_script.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "runtime/ee_scheduler.h"
#include "runtime/ps2_memory.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <iterator>
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

        void logFmvState(const uint8_t *rdram, const char *event);

        void logAnimPasses();

        // Frames the game has finished drawing (ClDisplay::end), for the frame-dump log.
        std::atomic<uint32_t> g_gameFrames{0};

#if TTR_HAS_GENERATED_CODE
        void countDisplayEnd(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            g_gameFrames.fetch_add(1u, std::memory_order_relaxed);
            end__9ClDisplayFv_0x17cf10(rdram, ctx, runtime);
        }
#endif

        void dumpFrameIfDue(uint8_t *rdram, PS2Runtime *runtime)
        {
            static uint32_t vblanks = 0;
            static const auto start = std::chrono::steady_clock::now();
            const FrameDumpConfig &config = frameDumpConfig();
            if (!config.enabled || (++vblanks % config.every) != 0u)
            {
                return;
            }
            // Since the previous dump: game frames and VBlanks per second of host time, and the share of
            // EE time that PS2X_REALTIME added to keep up with the host clock.
            static auto lastTime = start;
            static uint32_t lastFrames = 0u;
            static uint64_t lastSkipped = 0u;
            static uint64_t lastCycle = 0u;
            const auto now = std::chrono::steady_clock::now();
            const double interval = std::max(1e-6, std::chrono::duration<double>(now - lastTime).count());
            const uint32_t frames = g_gameFrames.load(std::memory_order_relaxed);
            const uint64_t skipped = runtime->eeScheduler().realtimeSkippedCycles();
            const uint64_t cycle = runtime->eeScheduler().eeCycle();
            char rates[96];
            std::snprintf(rates, sizeof(rates), " fps=%.1f vbps=%.1f rt-skip=%.0f%%", (frames - lastFrames) / interval,
                          config.every / interval,
                          cycle > lastCycle ? 100.0 * static_cast<double>(skipped - lastSkipped) / static_cast<double>(cycle - lastCycle) : 0.0);
            lastTime = now;
            lastFrames = frames;
            lastSkipped = skipped;
            lastCycle = cycle;
            char name[32];
            std::snprintf(name, sizeof(name), "frame_%06u.png", vblanks);
            const bool saved = ttr::debug::dumpPresentedFrame(*runtime, config.directory / name);
            const uint32_t current = ram32(rdram, kTheGame + 4u);
            const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            std::cout << "[ttr:frame] vblank=" << vblanks << " t=" << static_cast<int>(seconds) << "s" << rates << ' ' << name
                      << (saved ? "" : " (no frame)")
                      << " game.mode=" << ram32(rdram, kTheGame) << std::hex
                      << " game.current=0x" << current
                      << " vtbl=0x" << (current ? ram32(rdram, current) : 0u)
                      << " current.next=" << std::dec << (current ? ram32(rdram, current + 4u) : 0u)
                      << " drawFlags=" << static_cast<int>(rdram[0x381400u]) << static_cast<int>(rdram[0x381404u])
                      << static_cast<int>(rdram[0x381408u]) << static_cast<int>(rdram[0x38140Cu])
                      << static_cast<int>(rdram[0x381410u]) << std::endl;
            if (traceEnabled("anims"))
                logAnimPasses();
            if (traceEnabled("fmv"))
            {
                logFmvState(rdram, "frame");
            }

            static constexpr const char *kStatus[] = {"run", "ready", "wait", "wait-susp", "susp", "dormant"};
            static constexpr const char *kWait[] = {"-", "sleep", "sema", "evflag", "vsync", "external", "mpeg"};
            const EeKernelSnapshot snapshot = runtime->eeScheduler().snapshot();
            for (const EeThreadSnapshot &thread : snapshot.threads)
            {
                std::cout << "[ttr:thread] id=" << thread.id
                          << ' ' << kStatus[static_cast<int>(thread.status)]
                          << '/' << kWait[static_cast<int>(thread.waitReason)] << ':' << thread.waitId
                          << " prio=" << thread.currentPriority << std::hex
                          << " pc=0x" << thread.pc << " ra=0x" << thread.ra << " sp=0x" << thread.sp
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

        // TTR_GS_DUMP=<vblank>: records the GS debug history for 10 VBlanks and prints its last events.
        void dumpGsHistoryIfDue(PS2Runtime *runtime)
        {
            static const uint32_t dumpAt = []
            {
                const char *env = std::getenv("TTR_GS_DUMP");
                return env ? static_cast<uint32_t>(std::strtoul(env, nullptr, 0)) : 0u;
            }();
            static uint32_t vblanks = 0;
            if (dumpAt == 0u)
                return;
            ++vblanks;
            GS &gs = runtime->gs();
            if (vblanks == dumpAt)
            {
                gs.clearDebugHistory();
                gs.setDebugHistoryPaused(false);
                return;
            }
            if (vblanks != dumpAt + 10u) // the game builds a frame over several VBlanks
                return;
            gs.setDebugHistoryPaused(true);
            uint32_t draws = 0;
            uint32_t kinds[5] = {};
            const std::vector<GSDebugHistoryEntry> history = gs.getDebugHistory();
            for (const GSDebugHistoryEntry &e : history)
            {
                ++kinds[static_cast<int>(e.kind) % 5];
                if (e.kind == GSDebugEventKind::Register)
                {
                    std::cout << "[ttr:gs] reg 0x" << std::hex << static_cast<int>(e.reg) << " = 0x" << e.regValue
                              << std::dec << '\n';
                    continue;
                }
                if (e.kind != GSDebugEventKind::Draw)
                    continue;
                ++draws;
                std::cout << "[ttr:gs] draw prim=" << static_cast<int>(e.prim.type) << " tme=" << e.prim.tme
                          << " abe=" << e.prim.abe << " fst=" << e.prim.fst << " ctxt=" << e.prim.ctxt
                          << " n=" << e.vertexCount << " x=" << e.xMin << ".." << e.xMax << " y=" << e.yMin << ".."
                          << e.yMax << " z=" << e.zMin << ".." << e.zMax << " a=" << static_cast<int>(e.aMin) << ".."
                          << static_cast<int>(e.aMax) << std::hex << " fbp=0x" << e.frame.fbp << " fbw="
                          << e.frame.fbw << " psm=0x" << static_cast<int>(e.frame.psm) << " fbmsk=0x"
                          << e.frame.fbmsk << " zbp=0x" << e.zbuf.zbp << " zpsm=0x" << static_cast<int>(e.zbuf.psm)
                          << " zmsk=" << e.zbuf.zmask << " test=0x" << e.test << " alpha=0x" << e.alpha
                          << " tbp=0x" << e.tex0.tbp0 << " tpsm=0x" << static_cast<int>(e.tex0.psm) << std::dec
                          << " scissor=" << e.scissor.x0 << ".." << e.scissor.x1 << ',' << e.scissor.y0 << ".."
                          << e.scissor.y1 << '\n';
            }
            std::cout << "[ttr:gs] " << history.size() << " events: tags=" << kinds[0] << " regs=" << kinds[1]
                      << " draws=" << kinds[2] << " transfers=" << kinds[3] << " presents=" << kinds[4] << std::endl;
        }

        void traceVBlank(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            static uint32_t vblank = 0;
            ttr::debug::applyPadScript(++vblank);
            dumpGsHistoryIfDue(runtime);
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

        // Debug hack: any button skips the movies, as once they have been seen. Without a save the game makes
        // the intro unskippable (ClGameModePlay::enter passes skip mode 1; 0 = any button, 2 = Start).
        void hackSkippableFmv(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            SET_GPR_U32(ctx, 5, 0u);
            playModal__5ClFMVFPCcibP15ClSubtitleAsset_0x26a7a0(rdram, ctx, runtime);
        }

        // Phase E (docs/PLAN_60FPS.md): ClGameModePlay::enter turns on ClDisplay::CapTo30FPS, so gameplay draws at
        // most every second VBlank (25 fps in PAL). TTR_UNCAP_FPS=1 keeps that cap off.
        void hackNoFrameCap(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            SET_GPR_U32(ctx, 4, 0u);
            CapTo30FPS__9ClDisplayFb_0x17cb30(rdram, ctx, runtime);
        }

        // Debug hack: skips ClDisplayList::drawShadowAnims, the characters' shadow volumes.
        void hackSkipShadowVolumes(uint8_t *, R5900Context *ctx, PS2Runtime *)
        {
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

        // --- fmv: ClFMV (EA "FHDR"/"MPCh" movies fed to libmpeg from its nodata callback) ------
        constexpr uint32_t kFmvFlags = 0x003821D0u;       // smIsPlaying, smAbortPlayback, ... one byte every 4
        constexpr uint32_t kFmvFrameCount = 0x00382198u;  // smFrameCount
        constexpr uint32_t kFmvFileRemaining = 0x003821C4u;
        constexpr uint32_t kFmvSoundRequests = kGp - 0x70D8u;
        constexpr uint32_t kFmvMemory = 0x0038219Cu;  // smpFMVMemory, smFMVMemorySize, smpBuffer, ... chunk at +0x10
        constexpr uint32_t kFmvStreamBuffer = 0x0054C180u;  // ClFMVStreamBuffer {base, size, read, write, used}
        constexpr uint32_t kFmvSoundBuffer = 0x0054C200u;   // ClFMVSoundBuffer, same layout
        constexpr uint32_t kFmvMpegBuffer = 0x0054C280u;    // ClFMVStreamBuffer of "MPCh" chunks

        void logFmvBuffer(const uint8_t *rdram, const char *label, uint32_t buffer)
        {
            std::cout << ' ' << label << "{used=0x" << std::hex << ram32(rdram, buffer + 0x10u) << "/0x"
                      << ram32(rdram, buffer + 0x04u) << '}' << std::dec;
        }

        void logFmvState(const uint8_t *rdram, const char *event)
        {
            static constexpr const char *kFlags[] = {"playing", "abort", "finished", "eof", "soundEof", "videoEnd"};
            std::cout << "[ttr:fmv] " << event;
            for (uint32_t i = 0; i < 6u; ++i)
            {
                std::cout << ' ' << kFlags[i] << '=' << static_cast<int>(rdram[(kFmvFlags + i * 4u) & 0x01FFFFFFu]);
            }
            std::cout << " frames=" << ram32(rdram, kFmvFrameCount) << " remaining=" << ram32(rdram, kFmvFileRemaining)
                      << " soundReq=" << ram32(rdram, kFmvSoundRequests) << std::hex
                      << " memory=0x" << ram32(rdram, kFmvMemory) << "+0x" << ram32(rdram, kFmvMemory + 4u)
                      << " chunk=0x" << ram32(rdram, kFmvMemory + 0x10u) << std::dec;
            logFmvBuffer(rdram, "stream", kFmvStreamBuffer);
            logFmvBuffer(rdram, "sound", kFmvSoundBuffer);
            logFmvBuffer(rdram, "mpeg", kFmvMpegBuffer);
            std::cout << std::endl;
        }

        void traceFmvPlay(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            std::cout << "[ttr:fmv] play '" << guestString(rdram, GPR_U32(ctx, 4)) << "'" << std::endl;
            play__5ClFMVFPCc_0x26a5e0(rdram, ctx, runtime);
            logFmvState(rdram, "after-play");
        }

        void traceFmvAbort(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            logFmvState(rdram, "abort");
            abort__5ClFMVFv_0x26b2c0(rdram, ctx, runtime);
        }

        void traceFmvNoData(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            static uint32_t count = 0;
            const bool log = shouldLog(count);
            if (log)
                logFmvState(rdram, "nodata>");
            noDataCallback__5ClFMVFP7sceMpegP13sceMpegCbDataPv_0x26a4c0(rdram, ctx, runtime);
            if (log)
                std::cout << "[ttr:fmv] nodata #" << count << " -> " << GPR_U32(ctx, 2) << std::endl;
        }

        void traceFmvSoundRequest(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            static uint32_t count = 0;
            const bool log = shouldLog(count);
            const uint32_t target = GPR_U32(ctx, 4);
            const uint32_t size = GPR_U32(ctx, 5);
            handleSoundBufferRequest__5ClFMVFii_0x269fb0(rdram, ctx, runtime);
            if (log)
            {
                std::cout << "[ttr:fmv] sound request #" << count << " iop=0x" << std::hex << target << std::dec
                          << " size=" << size << " -> " << GPR_U32(ctx, 2) << std::endl;
            }
        }

        // --- anims: animated characters (ClDisplayList passes and Noam figure instances) ---------------
        constexpr const char *kAnimPassNames[] = {"shadow", "specular", "alpha", "instanced", "diffuseSpecular",
                                                 "diffuse", "figure", "addAnimation", "renderDiffuse",
                                                 "renderDiffuseSpecular", "ucodeRender", "ucodeLowQuality",
                                                 "actorAdd", "noamAdd", "propAdd", "particleAdd", "collAdd",
                                                 "animObjAdd", "scriptAdd", "pickupAdd"};
        uint32_t g_animCallers[std::size(kAnimPassNames)] = {};
        uint64_t g_animCalls[std::size(kAnimPassNames)] = {};

        template <int Index, void (*Function)(uint8_t *, R5900Context *, PS2Runtime *)>
        void countAnimPass(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            ++g_animCalls[Index];
            if (g_animCallers[Index] == 0u)
                g_animCallers[Index] = GPR_U32(ctx, 31);
            Function(rdram, ctx, runtime);
        }

        // Per-sector frustum registration (ClWorld draw) and lookups (ClSceneRegistration::renderObjects).
        uint64_t g_frustumAdds[64] = {};
        uint64_t g_frustumLookups[64] = {};
        uint64_t g_frustumHits[64] = {};
        uint64_t g_renderObjectNodes = 0;

        void traceFrustumToAdd(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            ++g_frustumAdds[GPR_U32(ctx, 4) & 63u];
            getFrustumToAdd__13ClFrustumListFi_0x26f880(rdram, ctx, runtime);
        }

        void traceFrustumListForSector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            const uint32_t sector = GPR_U32(ctx, 4) & 63u;
            getFrustumListForSector__13ClFrustumListFi_0x26f860(rdram, ctx, runtime);
            ++g_frustumLookups[sector];
            if (GPR_U32(ctx, 2) != 0u)
                ++g_frustumHits[sector];
        }

        void traceRenderObjectsRecursive(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            ++g_renderObjectNodes;
            renderObjectsRecursive__19ClSceneRegistrationFP20ClWorldCollisionNodeUiPC8ClCamerabb_0x240e70(rdram, ctx, runtime);
        }

        // ClFrustum::isBoundingBoxVisible(min, max, &clip): logs the first calls from the object renderer
        // (ClSceneRegistration::renderObjectsRecursive) and from the world (ClWorldNode::drawRecursive).
        void traceBoxVisible(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            const uint32_t ra = GPR_U32(ctx, 31);
            const uint32_t frustum = GPR_U32(ctx, 4), minAddr = GPR_U32(ctx, 5), maxAddr = GPR_U32(ctx, 6);
            const uint32_t clipAddr = GPR_U32(ctx, 7);
            float box[8], aabb[8];
            std::memcpy(box, rdram + (minAddr & 0x1FFFFFFu), 16);
            std::memcpy(box + 4, rdram + (maxAddr & 0x1FFFFFFu), 16);
            std::memcpy(aabb, rdram + ((frustum + 0x90u) & 0x1FFFFFFu), 32);
            isBoundingBoxVisible__9ClFrustumCFRC8ClVectorRC8ClVectorRi_0x19cc60(rdram, ctx, runtime);
            const bool objects = ra >= 0x00240E70u && ra < 0x00241200u;
            const bool world = ra >= 0x00239840u && ra < 0x0023A000u;
            static int objectLogs = 0, worldLogs = 0;
            int &logs = objects ? objectLogs : worldLogs;
            if ((!objects && !world) || logs >= 12)
                return;
            ++logs;
            std::cout << "[ttr:box] " << (objects ? "objects" : "world") << " ra=0x" << std::hex << ra << std::dec
                      << " min=(" << box[0] << ',' << box[1] << ',' << box[2] << ") max=(" << box[4] << ',' << box[5] << ','
                      << box[6] << ") frustumAabb=(" << aabb[0] << ',' << aabb[1] << ',' << aabb[2] << ")-(" << aabb[4] << ','
                      << aabb[5] << ',' << aabb[6] << ") -> " << GPR_U32(ctx, 2) << " clip=0x" << std::hex
                      << ram32(rdram, clipAddr & 0x1FFFFFFu) << std::dec << std::endl;
            // The same plane test in C++: corners in the order the VU0 code stores them, then for each plane
            // d = normal . corner[index] + distance for the two corner indices the plane carries.
            const float corners[8][3] = {{box[0], box[1], box[2]}, {box[4], box[1], box[2]}, {box[0], box[5], box[2]},
                                         {box[4], box[5], box[2]}, {box[0], box[1], box[6]}, {box[4], box[1], box[6]},
                                         {box[0], box[5], box[6]}, {box[4], box[5], box[6]}};
            std::cout << "[ttr:box]   planes";
            for (uint32_t plane = 0; plane < 6u; ++plane)
            {
                const uint32_t base = (frustum + 0x140u + plane * 0x20u) & 0x1FFFFFFu;
                float normal[4], distance;
                std::memcpy(normal, rdram + base, 16);
                std::memcpy(&distance, rdram + base + 0x10u, 4);
                const uint32_t near = ram32(rdram, base + 0x14u), far = ram32(rdram, base + 0x18u);
                auto dot = [&](uint32_t index)
                {
                    const float *c = corners[index & 7u];
                    return normal[0] * c[0] + normal[1] * c[1] + normal[2] * c[2] + distance;
                };
                std::cout << " n=(" << normal[0] << ',' << normal[1] << ',' << normal[2] << ") d=" << distance << " idx="
                          << near << '/' << far << " -> " << dot(near) << '/' << dot(far);
            }
            std::cout << std::endl;
        }

        void logAnimPasses()
        {
            std::cout << "[ttr:anims]";
            for (size_t i = 0; i < std::size(g_animCalls); ++i)
            {
                std::cout << ' ' << kAnimPassNames[i] << '=' << g_animCalls[i];
                if (g_animCallers[i] != 0u)
                    std::cout << "(ra=0x" << std::hex << g_animCallers[i] << std::dec << ')';
            }
            std::cout << std::endl;
            std::cout << "[ttr:anims] objectNodes=" << g_renderObjectNodes << " frustums(sector:added/lookups/hits)";
            for (int sector = 0; sector < 64; ++sector)
            {
                if (g_frustumAdds[sector] || g_frustumLookups[sector])
                    std::cout << ' ' << sector << ':' << g_frustumAdds[sector] << '/' << g_frustumLookups[sector] << '/'
                              << g_frustumHits[sector];
            }
            std::cout << std::endl;
        }

        // --- pad: ClPad slots (ClData::tickSlot reads libpad once per frame per port/slot) ----------
        // Slot state: +0x100 the scePadRead buffer, +0x120 status (3 = read), +0x128 buttons (active high),
        // +0x160/+0x164 left stick and +0x184/+0x188 right stick in [-1, 1].
        void tracePadSlot(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            const uint32_t port = GPR_U32(ctx, 5);
            const uint32_t slot = GPR_U32(ctx, 6);
            const uint32_t state = GPR_U32(ctx, 4) + port * 0x900u + slot * 0x240u;
            tickSlot__6ClDataFii_0x17e140(rdram, ctx, runtime);
            if (port != 0u || slot != 0u)
                return;
            static uint32_t lastStatus = ~0u, lastButtons = ~0u, lastMode = ~0u;
            static float lastStick[4] = {};
            const uint32_t status = ram32(rdram, state + 0x120u);
            const uint32_t buttons = ram32(rdram, state + 0x128u);
            const uint32_t mode = rdram[state + 0x101u];
            float stick[4];
            std::memcpy(&stick[0], rdram + state + 0x160u, 8);
            std::memcpy(&stick[2], rdram + state + 0x184u, 8);
            const bool stickChanged = std::memcmp(stick, lastStick, sizeof(stick)) != 0;
            if (status == lastStatus && buttons == lastButtons && mode == lastMode && !stickChanged)
                return;
            lastStatus = status;
            lastButtons = buttons;
            lastMode = mode;
            std::memcpy(lastStick, stick, sizeof(stick));
            std::cout << "[ttr:pad] slot status=" << status << " mode=0x" << std::hex << mode << " buttons=0x" << buttons
                      << std::dec << " left=(" << stick[0] << ',' << stick[1] << ") right=(" << stick[2] << ',' << stick[3]
                      << ')' << std::endl;
        }

        // --- camera: projection matrix inputs and the libm tanf it relies on ---------------------
        // World draw-node passes: how often each runs (they build the VU1 MSCALs for the world).
        uint64_t g_worldPassCalls[8] = {};

        void traceCameraProjection(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            static uint32_t count = 0;
            const bool log = shouldLog(count);
            const uint32_t camera = GPR_U32(ctx, 4);
            const float fov = ctx->f[12], aspect = ctx->f[13], nearPlane = ctx->f[14], farPlane = ctx->f[15];
            projection__8ClCameraFffff_0x17b200(rdram, ctx, runtime);
            if (log)
            {
                float m[16];
                std::memcpy(m, rdram + ((camera + 0x5E0u) & 0x01FFFFFFu), sizeof(m));
                std::cout << "[ttr:camera] cam=0x" << std::hex << camera << std::dec << " projection(fov=" << fov << " aspect=" << aspect << " near=" << nearPlane
                          << " far=" << farPlane << ") -> [" << m[0] << ' ' << m[5] << ' ' << m[10] << ' ' << m[11]
                          << ' ' << m[14] << ' ' << m[15] << ']' << std::endl;
            }
        }

        // SfMatrixMultiplyVu0(out, a, b) is VU0 macro code (VMULAx/VMADDAy/VMADDAz/VMADDw):
        // out[i] = a[i].x*b[0] + a[i].y*b[1] + a[i].z*b[2] + a[i].w*b[3]. Compare with plain C++.
        void checkMatrixMultiply(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            static uint32_t checked = 0, mismatches = 0;
            const uint32_t out = GPR_U32(ctx, 4), aAddr = GPR_U32(ctx, 5), bAddr = GPR_U32(ctx, 6);
            float a[16], b[16], expected[16], actual[16];
            std::memcpy(a, rdram + (aAddr & 0x01FFFFFFu), sizeof(a));
            std::memcpy(b, rdram + (bAddr & 0x01FFFFFFu), sizeof(b));
            for (int i = 0; i < 4; ++i)
                for (int j = 0; j < 4; ++j)
                    expected[i * 4 + j] = a[i * 4 + 0] * b[0 + j] + a[i * 4 + 1] * b[4 + j] + a[i * 4 + 2] * b[8 + j] +
                                          a[i * 4 + 3] * b[12 + j];
            SfMatrixMultiplyVu0__FR8ClMatrixRC8ClMatrixRC8ClMatrix_0x100210(rdram, ctx, runtime);
            std::memcpy(actual, rdram + (out & 0x01FFFFFFu), sizeof(actual));
            ++checked;
            bool same = true;
            for (int k = 0; k < 16; ++k)
            {
                const float tolerance = 1e-3f * std::max(1.0f, std::fabs(expected[k]));
                if (!(std::fabs(actual[k] - expected[k]) <= tolerance))
                    same = false;
            }
            if (!same && mismatches++ < 5u)
            {
                std::cout << "[ttr:vu0] SfMatrixMultiplyVu0 mismatch #" << mismatches << " (of " << checked << ")\n";
                for (int i = 0; i < 4; ++i)
                {
                    std::cout << "  a=" << a[i * 4] << ',' << a[i * 4 + 1] << ',' << a[i * 4 + 2] << ',' << a[i * 4 + 3]
                              << "  b=" << b[i * 4] << ',' << b[i * 4 + 1] << ',' << b[i * 4 + 2] << ',' << b[i * 4 + 3]
                              << "  want=" << expected[i * 4] << ',' << expected[i * 4 + 1] << ','
                              << expected[i * 4 + 2] << ',' << expected[i * 4 + 3] << "  got=" << actual[i * 4] << ','
                              << actual[i * 4 + 1] << ',' << actual[i * 4 + 2] << ',' << actual[i * 4 + 3] << '\n';
                }
                std::cout << std::flush;
            }
            if ((checked % 20000u) == 0u)
                std::cout << "[ttr:vu0] SfMatrixMultiplyVu0 checked=" << checked << " mismatches=" << mismatches << std::endl;
        }

        void logMatrix(const uint8_t *rdram, const char *label, uint32_t address)
        {
            float m[16];
            std::memcpy(m, rdram + (address & 0x01FFFFFFu), sizeof(m));
            std::cout << "  " << label << " @0x" << std::hex << address << std::dec << ':';
            for (int i = 0; i < 16; ++i)
                std::cout << (i % 4 == 0 ? " |" : " ") << m[i];
            std::cout << std::endl;
        }

        void traceWorldBeginDraw(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            static uint32_t count = 0;
            const uint32_t camera = GPR_U32(ctx, 6);
            if (count % 200u == 0u)
                std::cout << "[ttr:world] beginDraw=" << count << " single=" << g_worldPassCalls[0]
                          << " split=" << g_worldPassCalls[1] << " foliage=" << g_worldPassCalls[2]
                          << " decal=" << g_worldPassCalls[3] << " endDraw=" << g_worldPassCalls[4] << std::endl;
            {
                float draw[16];
                std::memcpy(draw, rdram + ((camera + 0x5A0u) & 0x01FFFFFFu), sizeof(draw));
                const bool zeroXY = draw[0] == 0.0f && draw[4] == 0.0f && draw[8] == 0.0f && draw[1] == 0.0f &&
                                    draw[5] == 0.0f && draw[9] == 0.0f;
                static uint32_t zeroLogs = 0;
                if (zeroXY && zeroLogs++ < 6u)
                {
                    std::cout << "[ttr:camera] beginDraw #" << count << " draw matrix has no X/Y" << std::endl;
                    logMatrix(rdram, "proj", camera + 0x5E0u);
                    logMatrix(rdram, "view", camera + 0x6A0u);
                    logMatrix(rdram, "clip", camera + 0x4E0u);
                    logMatrix(rdram, "draw", camera + 0x5A0u);
                }
            }
            if (shouldLog(count) && count % 4u == 1u)
            {
                std::cout << "[ttr:camera] ClWorldDrawData::beginDraw(" << GPR_U32(ctx, 5) << ", cam=0x" << std::hex
                          << camera << std::dec << ')' << std::endl;
                logMatrix(rdram, "proj", camera + 0x5E0u);
                logMatrix(rdram, "view", camera + 0x6A0u);
                logMatrix(rdram, "clip", camera + 0x4E0u);
                logMatrix(rdram, "draw", camera + 0x5A0u);
                std::cout << std::flush;
            }
            beginDraw__15ClWorldDrawDataFiRC8ClCamera_0x176800(rdram, ctx, runtime);
        }

        template <int Index, void (*Generated)(uint8_t *, R5900Context *, PS2Runtime *)>
        void countWorldPass(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            ++g_worldPassCalls[Index];
            Generated(rdram, ctx, runtime);
        }

        void traceWorldDraw(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            static uint32_t count = 0;
            if ((++count % 50u) == 1u)
            {
                std::cout << "[ttr:world] draw=" << count << " single=" << g_worldPassCalls[0]
                          << " split=" << g_worldPassCalls[1] << " foliage=" << g_worldPassCalls[2]
                          << " decal=" << g_worldPassCalls[3] << " endDraw=" << g_worldPassCalls[4] << std::endl;
            }
            draw__15ClWorldDrawDataFP11ClWorldNodeRC8ClCamerabiPQ221ClDynamicLightManager17ClEngineLightList_0x1763d0(rdram, ctx, runtime);
        }

        void traceZBufferValue(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            static uint32_t count = 0;
            const bool log = shouldLog(count);
            const uint32_t camera = GPR_U32(ctx, 4);
            const float depth = ctx->f[12];
            getZBufferValue__8ClCameraCFf_0x17c1b0(rdram, ctx, runtime);
            if (log)
            {
                std::cout << "[ttr:camera] getZBufferValue(cam=0x" << std::hex << camera << std::dec << ", " << depth
                          << ") = 0x" << std::hex << GPR_U32(ctx, 2) << std::dec << std::endl;
                if (count < 3u)
                {
                    logMatrix(rdram, "m660", camera + 0x660u);
                    logMatrix(rdram, "m560", camera + 0x560u);
                }
            }
        }

        void traceClearScreen(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            static uint32_t count = 0;
            if (shouldLog(count))
            {
                std::cout << "[ttr:camera] ClearScreen(" << (GPR_U32(ctx, 4) & 0xFF) << ',' << (GPR_U32(ctx, 5) & 0xFF) << ','
                          << (GPR_U32(ctx, 6) & 0xFF) << ", cam=0x" << std::hex << GPR_U32(ctx, 7) << std::dec << ") #" << count
                          << " ra=0x" << std::hex << GPR_U32(ctx, 31) << std::dec << std::endl;
            }
            ClearScreen__9ClDisplayFUcUcUcPC8ClCamera_0x17ca20(rdram, ctx, runtime);
        }

        void tracePbiClear(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            static uint32_t count = 0;
            if (++count > 2000u && count < 2060u)
            {
                std::cout << "[ttr:camera] ClPbi::clear(pbi=0x" << std::hex << GPR_U32(ctx, 4) << ", mask=0x" << GPR_U32(ctx, 5)
                          << ", " << (GPR_U32(ctx, 6) & 0xFF) << ',' << (GPR_U32(ctx, 7) & 0xFF) << ',' << (GPR_U32(ctx, 8) & 0xFF)
                          << ',' << (GPR_U32(ctx, 9) & 0xFF) << ") ra=0x" << GPR_U32(ctx, 31) << std::dec << std::endl;
            }
            clear__5ClPbiFUiUiUiUiUi_0x16d860(rdram, ctx, runtime);
        }

        void traceTanf(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            static uint32_t count = 0;
            const bool log = shouldLog(count);
            const float x = ctx->f[12];
            tanf_0x2b8ee0(rdram, ctx, runtime);
            if (log)
                std::cout << "[ttr:camera] tanf(" << x << ") = " << ctx->f[0] << std::endl;
        }

        void traceFmvDraw(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            static uint32_t last = 0xFFFFFFFFu;
            static uint32_t repeats = 0;
            draw__5ClFMVFb_0x26b150(rdram, ctx, runtime);
            const uint32_t result = GPR_U32(ctx, 2);
            if (result != last)
            {
                std::cout << "[ttr:fmv] draw -> " << result << " (previous repeated " << repeats << "x)" << std::endl;
                logFmvState(rdram, "draw");
                last = result;
                repeats = 0;
            }
            else
            {
                ++repeats;
            }
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
        if (const char *hack = std::getenv("TTR_HACK_NO_SHADOW_VOLUMES"); hack && hack[0] == '1')
        {
            runtime.replaceFunction(0x00254350u, &hackSkipShadowVolumes);
            std::cout << "[ttr] hack: no character shadow volumes" << std::endl;
        }
        if (const char *uncap = std::getenv("TTR_UNCAP_FPS"); uncap && uncap[0] == '1')
        {
            runtime.replaceFunction(0x0017CB30u, &hackNoFrameCap);
            std::cout << "[ttr] gameplay not capped to half the VBlank rate" << std::endl;
        }
        if (const char *hack = std::getenv("TTR_HACK_SKIPPABLE_FMV"); hack && hack[0] == '1')
        {
            runtime.replaceFunction(0x0026A7A0u, &hackSkippableFmv);
            std::cout << "[ttr] hack: any button skips the movies" << std::endl;
        }
        if (traceEnabled("rpc"))
        {
            runtime.replaceFunction(0x002AE558u, &traceBindRpc);
            runtime.replaceFunction(0x002AE728u, &traceCallRpc);
            runtime.replaceFunction(0x002AE918u, &traceCheckStatRpc);
            std::cout << "[ttr] SIF RPC tracing enabled" << std::endl;
        }
        if (traceEnabled("anims"))
        {
            runtime.replaceFunction(0x00254350u, &countAnimPass<0, drawShadowAnims__13ClDisplayListFRC8ClCamera_0x254350>);
            runtime.replaceFunction(0x002547B0u, &countAnimPass<1, drawSpecularAnims__13ClDisplayListFRC8ClCamera_0x2547b0>);
            runtime.replaceFunction(0x00254960u, &countAnimPass<2, drawAlphaAnims__13ClDisplayListFRC8ClCamera_0x254960>);
            runtime.replaceFunction(0x00254B50u, &countAnimPass<3, drawInstancedAnims__13ClDisplayListFRC8ClCamera_0x254b50>);
            runtime.replaceFunction(0x00254D70u, &countAnimPass<4, drawDiffuseAndSpecularAnims__13ClDisplayListFRC8ClCamerab_0x254d70>);
            runtime.replaceFunction(0x00255250u, &countAnimPass<5, drawDiffuseAnims__13ClDisplayListFRC8ClCamerab_0x255250>);
            runtime.replaceFunction(0x001A2290u, &countAnimPass<6, drawAnimationInstance__15ClNoamFigurePs2FR8ClMatrixRC8ClCamera_0x1a2290>);
            runtime.replaceFunction(0x00256560u, &countAnimPass<7, addAnimation__13ClDisplayListFP15ClNoamFigurePs2RC8ClMatrixP17ClPS2TextureAssetP17ClPS2TextureAssetUiPQ221ClDynamicLightManager17ClEngineLightList_0x256560>);
            runtime.replaceFunction(0x001A2AF0u, &countAnimPass<8, RenderDiffuse__15ClNoamFigurePs2FRC8ClCameraR8ClMatrixP5ClPbi_0x1a2af0>);
            runtime.replaceFunction(0x001A26E0u, &countAnimPass<9, RenderDiffuseAndSpecular__15ClNoamFigurePs2FRC8ClCameraR8ClMatrixP5ClPbiP5ClPbi_0x1a26e0>);
            runtime.replaceFunction(0x00285E80u, &countAnimPass<10, render__23ClNoamPs2UcodeInterfaceFP9CModelBinP8ClMatrixUi_0x285e80>);
            runtime.replaceFunction(0x00285CC0u, &countAnimPass<11, renderLowQuality__23ClNoamPs2UcodeInterfaceFP9CModelBin_0x285cc0>);
            runtime.replaceFunction(0x00125E90u, &countAnimPass<12, AddToDisplayList__6CActorFUiii_0x125e90>);
            runtime.replaceFunction(0x001BEE30u, &countAnimPass<13, AddToDisplayList__16ClNoamControllerFUib_0x1bee30>);
            runtime.replaceFunction(0x00226B90u, &countAnimPass<14, AddToDisplayList__6ClPropFUiii_0x226b90>);
            runtime.replaceFunction(0x002004F0u, &countAnimPass<15, AddToDisplayList__13ClParticleObjFUiii_0x2004f0>);
            runtime.replaceFunction(0x00134110u, &countAnimPass<16, AddToDisplayList__8CCollObjFUiii_0x134110>);
            runtime.replaceFunction(0x001284D0u, &countAnimPass<17, AddToDisplayList__8CAnimObjFUiii_0x1284d0>);
            runtime.replaceFunction(0x0011C030u, &countAnimPass<18, AddToDisplayList__11ClScriptObjFUiii_0x11c030>);
            runtime.replaceFunction(0x0022FB70u, &countAnimPass<19, AddToDisplayList__11ClPickupObjFUiii_0x22fb70>);
            runtime.replaceFunction(0x0026F880u, &traceFrustumToAdd);
            runtime.replaceFunction(0x0026F860u, &traceFrustumListForSector);
            runtime.replaceFunction(0x00240E70u, &traceRenderObjectsRecursive);
            runtime.replaceFunction(0x0019CC60u, &traceBoxVisible);
            std::cout << "[ttr] anim pass counting enabled" << std::endl;
        }
        if (traceEnabled("pad"))
        {
            runtime.replaceFunction(0x0017E140u, &tracePadSlot);
            std::cout << "[ttr] pad tracing enabled" << std::endl;
        }
        if (traceEnabled("camera"))
        {
            runtime.replaceFunction(0x0017B200u, &traceCameraProjection);
            runtime.replaceFunction(0x002B8EE0u, &traceTanf);
            runtime.replaceFunction(0x00100210u, &checkMatrixMultiply);
            runtime.replaceFunction(0x00176800u, &traceWorldBeginDraw);
            runtime.replaceFunction(0x00174470u, &countWorldPass<0, drawSingleTexture__10ClDrawNodeFP20ClDrawNodeListHeaderP5ClPbi_0x174470>);
            runtime.replaceFunction(0x00174010u, &countWorldPass<1, drawSplitTexture__10ClDrawNodeFP20ClDrawNodeListHeaderQ28ClTexEnv16eTextureFunctionP5ClPbiP5ClPbiP5ClPbi_0x174010>);
            runtime.replaceFunction(0x00172770u, &countWorldPass<2, drawFoliage__10ClDrawNodeFP20ClDrawNodeListHeaderP5ClPbi_0x172770>);
            runtime.replaceFunction(0x00173900u, &countWorldPass<3, drawDecal__10ClDrawNodeFP20ClDrawNodeListHeaderP5ClPbi_0x173900>);
            runtime.replaceFunction(0x001750D0u, &countWorldPass<4, endDraw__15ClWorldDrawDataFPQ27ClWorld18ClTextureContainer_0x1750d0>);
            runtime.replaceFunction(0x001763D0u, &traceWorldDraw);
            runtime.replaceFunction(0x0017C1B0u, &traceZBufferValue);
            runtime.replaceFunction(0x0017CA20u, &traceClearScreen);
            runtime.replaceFunction(0x0016D860u, &tracePbiClear);
            std::cout << "[ttr] camera tracing enabled" << std::endl;
        }
        if (traceEnabled("fmv"))
        {
            runtime.replaceFunction(0x0026A5E0u, &traceFmvPlay);
            runtime.replaceFunction(0x0026B2C0u, &traceFmvAbort);
            runtime.replaceFunction(0x0026A4C0u, &traceFmvNoData);
            runtime.replaceFunction(0x00269FB0u, &traceFmvSoundRequest);
            runtime.replaceFunction(0x0026B150u, &traceFmvDraw);
            std::cout << "[ttr] FMV tracing enabled" << std::endl;
        }
        if (traceEnabled("dma") || frameDumpConfig().enabled || std::getenv("TTR_GS_DUMP") ||
            ttr::debug::padScriptEnabled())
        {
            runtime.replaceFunction(0x00106290u, &traceVBlank);
        }
        if (frameDumpConfig().enabled)
        {
            runtime.replaceFunction(0x0017CF10u, &countDisplayEnd);
            std::cout << "[ttr] dumping a frame every " << frameDumpConfig().every << " VBlanks to "
                      << frameDumpConfig().directory.string() << std::endl;
        }
#else
        (void)runtime;
#endif
    }
}
