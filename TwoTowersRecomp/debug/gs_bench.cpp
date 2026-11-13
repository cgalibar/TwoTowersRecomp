// ttr_gs_bench: replays a GS capture (PS2X_GS_CAPTURE=<file>,<vblank>,<presents>) on the software GS.
//
//   ttr_gs_bench <capture> [--repeat <n>]        time the replay (best of n), print ms per game frame
//   ttr_gs_bench <capture> --hashes <file>        also write one VRAM hash per presentation (flushes there)
//   ttr_gs_bench --compare-hashes <a> <b>         first presentation whose hashes differ
//   ttr_gs_bench <capture> --dump-vram <file>     write VRAM after the replay
//   ttr_gs_bench <capture> --gpu-uploads         host-to-local transfers only, on the CPU and the GPU: same VRAM?
//   ttr_gs_bench <capture> --gpu-selftest        OpenGL compute context: VRAM through the GPU and back
//   ttr_gs_bench <capture> --gpu                 replay on the GS on the GPU (GSGpuBackend); with --check-hashes
//                                                 it is compared with a reference made by the CPU backend
//   ttr_gs_bench --selftest                      check the row/column swizzle tables against GSMem
//   ttr_gs_bench --compare-vram <a> <b>           differing bytes and pages of two VRAM dumps
//   ttr_gs_bench <capture> --png <prefix>         write every presented frame as <prefix><n>.png
//   ttr_gs_bench <capture> --presents <n>       stop after n presentations (any mode)
//   ttr_gs_bench <capture> --list-presents       VBlank, primitives and display registers per presentation
//   ttr_gs_bench <capture> --check-hashes <file>  write diff_<n>.vram at each presentation whose hash differs
//   ttr_gs_bench <capture> --dump-at <n>          write at_<n>.vram at presentation n
//   ttr_gs_bench <capture> --stats                pixels and primitives per class of GS state (no drawing)
//   ttr_gs_bench <capture> --skinny <n>           long thin triangles (streaks) submitted before presentation n
//
// The raster threads follow PS2X_GS_THREADS as in the game (0 = everything on the calling thread, the
// deterministic reference). TTR_PROFILE works as in the game.
#include "runtime/gs/gs_capture.h"
#include "runtime/gs/gs_cpu_backend.h"
#include "runtime/gs/gs_gpu_backend.h"
#include "runtime/gs/gs_gpu_context.h"
#include "runtime/gs/gs_swizzle.h"
#include "debug/sampling_profiler.h"

#include "raylib.h"

#if defined(_WIN32)
extern "C" __declspec(dllimport) int __stdcall GetProcessTimes(void *process, void *creation, void *exit, void *kernel, void *user);
extern "C" __declspec(dllimport) void *__stdcall GetCurrentProcess();
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace
{
    // User + kernel CPU time of the process, in milliseconds.
    double processCpuMs()
    {
#if defined(_WIN32)
        uint64_t creation = 0, exit = 0, kernel = 0, user = 0;
        if (GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user))
            return static_cast<double>(kernel + user) / 10000.0;
#endif
        return 0.0;
    }

    uint64_t hashBytes(const uint8_t *data, size_t size)
    {
        uint64_t hash = 0xCBF29CE484222325ull;
        size_t i = 0;
        for (; i + 8u <= size; i += 8u)
        {
            uint64_t word;
            std::memcpy(&word, data + i, 8u);
            hash = (hash ^ word) * 0x100000001B3ull;
            hash ^= hash >> 29u;
        }
        for (; i < size; ++i)
            hash = (hash ^ data[i]) * 0x100000001B3ull;
        return hash;
    }

    bool readFile(const char *path, std::vector<uint8_t> &out)
    {
        FILE *file = std::fopen(path, "rb");
        if (!file)
            return false;
        std::fseek(file, 0, SEEK_END);
        out.resize(static_cast<size_t>(std::ftell(file)));
        std::fseek(file, 0, SEEK_SET);
        const bool ok = std::fread(out.data(), 1u, out.size(), file) == out.size();
        std::fclose(file);
        return ok;
    }

    bool writeFile(const std::string &path, const void *data, size_t size)
    {
        FILE *file = std::fopen(path.c_str(), "wb");
        if (!file)
            return false;
        const bool ok = std::fwrite(data, 1u, size, file) == size;
        std::fclose(file);
        return ok;
    }

    int compareHashes(const char *a, const char *b)
    {
        std::vector<uint8_t> left, right;
        if (!readFile(a, left) || !readFile(b, right))
        {
            std::printf("cannot read the hash files\n");
            return 2;
        }
        const size_t count = std::min(left.size(), right.size()) / 8u;
        size_t differing = 0, first = SIZE_MAX;
        for (size_t i = 0; i < count; ++i)
        {
            if (std::memcmp(left.data() + i * 8u, right.data() + i * 8u, 8u) != 0)
            {
                ++differing;
                first = std::min(first, i);
            }
        }
        if (left.size() != right.size())
            std::printf("different lengths: %zu and %zu presentations\n", left.size() / 8u, right.size() / 8u);
        if (differing == 0u)
            std::printf("same: %zu presentations\n", count);
        else
            std::printf("DIFFERENT: %zu of %zu presentations, first at presentation %zu\n", differing, count, first);
        return differing == 0u && left.size() == right.size() ? 0 : 1;
    }

    int compareVram(const char *a, const char *b)
    {
        std::vector<uint8_t> left, right;
        if (!readFile(a, left) || !readFile(b, right) || left.size() != right.size())
        {
            std::printf("cannot read the VRAM dumps (or they differ in size)\n");
            return 2;
        }
        size_t differing = 0, shown = 0;
        std::map<size_t, size_t> pages;
        for (size_t i = 0; i < left.size(); ++i)
        {
            if (left[i] == right[i])
                continue;
            ++differing;
            ++pages[i / 8192u];
            if (shown < 20u)
            {
                std::printf("  0x%06zx (page %zu): %02x %02x\n", i, i / 8192u, left[i], right[i]);
                ++shown;
            }
        }
        std::printf("%zu bytes differ in %zu pages\n", differing, pages.size());
        for (const auto &[page, count] : pages)
            std::printf("  page %zu (block %zu): %zu bytes\n", page, page * 32u, count);
        return differing == 0u ? 0 : 1;
    }

    // --stats: GS state classes by pixel area, to choose what to specialize.
    struct ClassStats
    {
        uint64_t primitives = 0;
        double pixels = 0.0;
    };

    std::string classify(const GSPrimitiveBatch &batch, double &area)
    {
        const GSDrawState &s = batch.state;
        const GSContext &c = s.context;
        const float ofx = static_cast<float>(c.xyoffset.ofx >> 4u);
        const float ofy = static_cast<float>(c.xyoffset.ofy >> 4u);
        float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
        const unsigned count = std::min<unsigned>(batch.vertexCount, 3u);
        for (unsigned i = 0; i < count; ++i)
        {
            minX = std::min(minX, batch.vertices[i].x - ofx);
            maxX = std::max(maxX, batch.vertices[i].x - ofx);
            minY = std::min(minY, batch.vertices[i].y - ofy);
            maxY = std::max(maxY, batch.vertices[i].y - ofy);
        }
        minX = std::max(minX, static_cast<float>(c.scissor.x0));
        minY = std::max(minY, static_cast<float>(c.scissor.y0));
        maxX = std::min(maxX, static_cast<float>(c.scissor.x1) + 1.0f);
        maxY = std::min(maxY, static_cast<float>(c.scissor.y1) + 1.0f);
        area = (maxX > minX && maxY > minY) ? static_cast<double>(maxX - minX) * (maxY - minY) : 0.0;

        const char *type = "point";
        switch (s.prim.type)
        {
        case GS_PRIM_LINE:
        case GS_PRIM_LINESTRIP:
            type = "line";
            break;
        case GS_PRIM_TRIANGLE:
        case GS_PRIM_TRISTRIP:
        case GS_PRIM_TRIFAN:
            type = "tri";
            area *= 0.5;
            break;
        case GS_PRIM_SPRITE:
            type = "sprite";
            break;
        default:
            break;
        }
        const uint64_t test = c.test;
        const bool ate = (test & 1u) != 0u;
        const bool date = ((test >> 14u) & 1u) != 0u;
        const bool zte = ((test >> 16u) & 1u) != 0u;
        const unsigned ztst = static_cast<unsigned>((test >> 17u) & 3u);
        char text[256];
        int n = std::snprintf(text, sizeof(text), "%-6s fb=%u%s z=%s", type, c.frame.psm, c.frame.fbmsk ? "/msk" : "",
                              zte ? "" : "off");
        if (zte)
            n += std::snprintf(text + n, sizeof(text) - n, "%u/t%u%s", c.zbuf.psm, ztst, c.zbuf.zmask ? "/nowrite" : "");
        if (ate)
            n += std::snprintf(text + n, sizeof(text) - n, " atst=%u/fail%u", static_cast<unsigned>((test >> 1u) & 7u),
                               static_cast<unsigned>((test >> 12u) & 3u));
        if (date)
            n += std::snprintf(text + n, sizeof(text) - n, " date");
        if (s.prim.abe)
        {
            const uint64_t a = c.alpha;
            n += std::snprintf(text + n, sizeof(text) - n, " blend=%u%u%u%u", static_cast<unsigned>(a & 3u),
                               static_cast<unsigned>((a >> 2u) & 3u), static_cast<unsigned>((a >> 4u) & 3u),
                               static_cast<unsigned>((a >> 6u) & 3u));
        }
        if (s.prim.tme)
            n += std::snprintf(text + n, sizeof(text) - n, " tex=%u%s tfx=%u tcc=%u%s", c.tex0.psm,
                               s.linearFilter ? "/lin" : "", c.tex0.tfx, c.tex0.tcc, s.prim.fst ? " uv" : " stq");
        if (s.prim.fge)
            n += std::snprintf(text + n, sizeof(text) - n, " fog");
        if (s.prim.iip)
            n += std::snprintf(text + n, sizeof(text) - n, " gouraud");
        return text;
    }

    struct Options
    {
        const char *capture = nullptr;
        int repeat = 1;
        std::string hashesPath;
        std::string vramPath;
        std::string pngPrefix;
        bool stats = false;
        bool listPresents = false;
        bool gpuSelfTest = false;
        bool gpuUploads = false;
        bool gpu = false;
        uint64_t maxPresents = 0;
        std::string checkHashesPath;
        uint64_t dumpAt = ~0ull;
        int touchingPage = -1;
        int64_t skinnyPresent = -1;
        uint64_t fromPresent = 0;
    };

    // Pages of the pixel rectangle [x0, x1] x [y0, y1] of a buffer (as GSCpuBackend's markRect).
    void pagesOfRect(std::vector<bool> &pages, uint32_t bp, uint32_t bw, uint32_t psm, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1)
    {
        uint32_t pw = 64u, ph = 32u;
        switch (psm)
        {
        case GS_PSM_T4: pw = 128u; ph = 128u; break;
        case GS_PSM_T8: pw = 128u; ph = 64u; break;
        case GS_PSM_CT16: case GS_PSM_CT16S: case GS_PSM_Z16: case GS_PSM_Z16S: pw = 64u; ph = 64u; break;
        default: break;
        }
        const uint32_t perRow = std::max<uint32_t>((std::max<uint32_t>(bw, 1u) * 64u + pw - 1u) / pw, 1u);
        const uint32_t spill = (bp & 31u) != 0u ? 1u : 0u;
        for (uint32_t row = y0 / ph; row <= y1 / ph; ++row)
            for (uint32_t col = x0 / pw; col <= x1 / pw + spill; ++col)
                pages[((bp >> 5u) + row * perRow + col) & 511u] = true;
    }
}

int main(int argc, char **argv)
{
    ttr::debug::startSamplingProfilerFromEnvironment();
    Options options;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--compare-hashes" && i + 2 < argc)
            return compareHashes(argv[i + 1], argv[i + 2]);
        if (arg == "--compare-vram" && i + 2 < argc)
            return compareVram(argv[i + 1], argv[i + 2]);
        if (arg == "--selftest")
        {
            std::string report;
            const bool ok = GSSwizzle::SelfTest(report);
            std::printf("swizzle tables: %s %s\n", ok ? "OK" : "FAILED", report.c_str());
            return ok ? 0 : 1;
        }
        if (arg == "--repeat" && i + 1 < argc)
            options.repeat = std::max(1, std::atoi(argv[++i]));
        else if (arg == "--hashes" && i + 1 < argc)
            options.hashesPath = argv[++i];
        else if (arg == "--dump-vram" && i + 1 < argc)
            options.vramPath = argv[++i];
        else if (arg == "--png" && i + 1 < argc)
            options.pngPrefix = argv[++i];
        else if (arg == "--stats")
            options.stats = true;
        else if (arg == "--presents" && i + 1 < argc)
            options.maxPresents = std::strtoull(argv[++i], nullptr, 10);
        else if (arg == "--check-hashes" && i + 1 < argc)
            options.checkHashesPath = argv[++i];
        else if (arg == "--dump-at" && i + 1 < argc)
            options.dumpAt = std::strtoull(argv[++i], nullptr, 10);
        else if (arg == "--touching" && i + 1 < argc)
            options.touchingPage = static_cast<int>(std::strtol(argv[++i], nullptr, 10));
        else if (arg == "--skinny" && i + 1 < argc)
            options.skinnyPresent = std::strtoll(argv[++i], nullptr, 10);
        else if (arg == "--from" && i + 1 < argc)
            options.fromPresent = std::strtoull(argv[++i], nullptr, 10);
        else if (arg == "--gpu-uploads")
            options.gpuUploads = true;
        else if (arg == "--gpu-selftest")
            options.gpuSelfTest = true;
        else if (arg == "--gpu")
            options.gpu = true;
        else if (arg == "--list-presents")
            options.listPresents = true;
        else if (!options.capture && arg[0] != '-')
            options.capture = argv[i];
        else
        {
            std::printf("unknown argument %s\n", argv[i]);
            return 2;
        }
    }
    if (!options.capture)
    {
        std::printf("usage: ttr_gs_bench <capture> [--repeat n] [--hashes file] [--dump-vram file] [--png prefix] [--stats]\n"
                    "       ttr_gs_bench --compare-hashes <a> <b> | --compare-vram <a> <b>\n");
        return 2;
    }

    GSCapture::Capture capture;
    std::string error;
    if (!GSCapture::Load(options.capture, capture, error))
    {
        std::printf("%s: %s\n", options.capture, error.c_str());
        return 2;
    }
    std::printf("capture: VBlank %u, %.1f MB of records\n", capture.header.firstVblank,
                static_cast<double>(capture.stream.size()) / 1048576.0);

    if (options.gpuUploads)
    {
        // G1: only the host-to-local transfers of the capture, on the CPU (reference) and on the GPU.
        struct UploadsOnly final : GSRasterBackend
        {
            GSCpuBackend *cpu = nullptr;
            GSGpuBackend *gpu = nullptr;
            uint64_t transfers = 0, chunks = 0, bytes = 0;
            void Initialize(uint8_t *, uint32_t) override {}
            void Reset() override {}
            void Submit(const GSPrimitiveBatch &) override {}
            void LoadClut(const GSTex0Reg &, const GSTexClutReg &) override {}
            void BeginTransfer(const GSTransferCommand &command) override
            {
                if (command.direction != 0u)
                    return; // local-to-local or local-to-host: left out on both sides
                ++transfers;
                cpu->BeginTransfer(command);
                gpu->BeginTransfer(command);
            }
            void UploadImage(const uint8_t *data, uint32_t size) override
            {
                ++chunks;
                bytes += size;
                cpu->UploadImage(data, size);
                gpu->UploadImage(data, size);
            }
            void Flush() override {}
            void TextureFlush() override {}
            void Sync(GSSyncReason) override {}
            PresentationFrame Present(const GSPresentationRequest &) override { return {}; }
            bool ClearFramebuffer(const GSContext &, uint32_t) override { return false; }
            uint32_t ConsumeLocalToHostBytes(uint8_t *, uint32_t) override { return 0u; }
            uint32_t ReadVram(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) const override { return 0u; }
            void WriteVram(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) override {}
            void SnapshotVram(std::vector<uint8_t> &) const override {}
            GSTransferSnapshot GetTransferSnapshot() const override { return {}; }
        };
        std::string error;
        std::unique_ptr<GSGpuBackend> gpu = GSGpuBackend::Create(error);
        if (!gpu)
        {
            std::printf("no GPU backend: %s\n", error.c_str());
            return 1;
        }
        std::printf("GPU: %s\n", gpu->Context().Renderer().c_str());
        std::vector<uint8_t> cpuVram = capture.vram;
        GSCpuBackend cpu(false);
        cpu.Initialize(cpuVram.data(), static_cast<uint32_t>(cpuVram.size()));
        gpu->LoadVram(capture.vram.data(), capture.vram.size());
        UploadsOnly uploads;
        uploads.cpu = &cpu;
        uploads.gpu = gpu.get();
        GSCapture::ReplayHooks hooks;
        hooks.maxPresents = options.maxPresents;
        const auto start = std::chrono::steady_clock::now();
        GSCapture::Replay(capture, &uploads, hooks);
        std::vector<uint8_t> gpuVram;
        gpu->ReadVramAll(gpuVram);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        cpu.Flush();
        size_t differing = 0, first = SIZE_MAX;
        for (size_t i = 0; i < cpuVram.size(); ++i)
            if (cpuVram[i] != gpuVram[i])
            {
                ++differing;
                first = std::min(first, i);
            }
        std::printf("%llu transfers, %llu chunks, %.1f MB in %.0f ms (CPU and GPU together): ",
                    static_cast<unsigned long long>(uploads.transfers), static_cast<unsigned long long>(uploads.chunks),
                    static_cast<double>(uploads.bytes) / 1048576.0, ms);
        if (differing == 0u)
            std::printf("VRAM identical\n");
        else
            std::printf("%zu bytes DIFFER, first at 0x%zx (page %zu)\n", differing, first, first / 8192u);
        return differing == 0u ? 0 : 1;
    }

    if (options.gpuSelfTest)
    {
        // G0 (docs/PLAN_60FPS.md 6.3): compute context, the capture's VRAM through the GPU and back.
        std::string error;
        std::unique_ptr<GSGpuContext> gpu = GSGpuContext::Create(error);
        if (!gpu)
        {
            std::printf("no GPU context: %s\n", error.c_str());
            return 1;
        }
        gpu->MakeCurrent();
        std::printf("GPU: %s\n", gpu->Renderer().c_str());
        const uint32_t words = static_cast<uint32_t>(capture.vram.size() / 4u);
        const uint32_t source = gpu->CreateBuffer(capture.vram.size(), capture.vram.data());
        const uint32_t target = gpu->CreateBuffer(capture.vram.size());
        const uint32_t program = gpu->CreateProgram(R"(
layout(local_size_x = 256) in;
layout(std430, binding = 0) readonly buffer Source { uint source[]; };
layout(std430, binding = 1) writeonly buffer Target { uint target[]; };
uniform uint words;
void main()
{
    uint i = gl_GlobalInvocationID.x;
    if (i < words)
        target[i] = source[i];
}
)",
                                                    error);
        if (!program)
        {
            std::printf("%s\n", error.c_str());
            return 1;
        }
        gpu->UseProgram(program);
        gpu->SetUniform(program, "words", words);
        gpu->BindStorage(0, source);
        gpu->BindStorage(1, target);
        const auto start = std::chrono::steady_clock::now();
        gpu->Dispatch((words + 255u) / 256u);
        gpu->Barrier();
        std::vector<uint8_t> back(capture.vram.size());
        gpu->Download(target, 0, back.size(), back.data());
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        const bool same = back == capture.vram;
        std::printf("VRAM through the GPU: %s (dispatch + 4 MB read back: %.2f ms)\n", same ? "identical" : "DIFFERENT", ms);
        gpu->DeleteBuffer(source);
        gpu->DeleteBuffer(target);
        return same ? 0 : 1;
    }

    if (options.skinnyPresent >= 0)
    {
        GSCapture::ReplayHooks hooks;
        hooks.maxPresents = static_cast<uint64_t>(options.skinnyPresent) + 1u;
        uint64_t present = 0, index = 0, inPresent = 0;
        hooks.onPresent = [&](const GSPresentationRequest &, const PresentationFrame *) { ++present; inPresent = 0; };
        hooks.onSubmit = [&](const GSPrimitiveBatch &batch)
        {
            ++index;
            ++inPresent;
            // TTR_BENCH_RECT=x0,y0,x1,y1: every primitive (sprites too) whose box meets that window rectangle instead.
            static int rect[4] = {0, 0, -1, -1};
            static const bool byRect = [] {
                const char *r = std::getenv("TTR_BENCH_RECT");
                return r && std::sscanf(r, "%d,%d,%d,%d", &rect[0], &rect[1], &rect[2], &rect[3]) == 4;
            }();
            if (present != static_cast<uint64_t>(options.skinnyPresent) || batch.vertexCount < (byRect ? 1u : 3u))
                return;
            const GSVertex *v = batch.vertices.data();
            const GSContext &cx = batch.state.context;
            if (byRect)
            {
                float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
                for (unsigned i = 0; i < batch.vertexCount; ++i)
                {
                    x0 = std::min(x0, v[i].x - cx.xyoffset.ofx / 16.0f);
                    x1 = std::max(x1, v[i].x - cx.xyoffset.ofx / 16.0f);
                    y0 = std::min(y0, v[i].y - cx.xyoffset.ofy / 16.0f);
                    y1 = std::max(y1, v[i].y - cx.xyoffset.ofy / 16.0f);
                }
                if (x1 < rect[0] || x0 > rect[2] || y1 < rect[1] || y0 > rect[3])
                    return;
                const GSDrawState &st = batch.state;
                std::printf("prim %llu type %u n %u tme %u fst %u abe %u tex %u/w%u/psm%u %ux%u tex1 %llx clamp %llx linear %u "
                            "alpha %llx test %llx fb %u/%u/psm%u\n",
                            static_cast<unsigned long long>(index), st.prim.type, batch.vertexCount, st.prim.tme, st.prim.fst,
                            st.prim.abe, cx.tex0.tbp0, cx.tex0.tbw, cx.tex0.psm, st.textureWidth, st.textureHeight,
                            static_cast<unsigned long long>(cx.tex1), static_cast<unsigned long long>(cx.clamp),
                            st.linearFilter ? 1u : 0u, static_cast<unsigned long long>(cx.alpha),
                            static_cast<unsigned long long>(cx.test), cx.frame.fbp, cx.frame.fbw, cx.frame.psm);
                for (unsigned i = 0; i < batch.vertexCount; ++i)
                    std::printf("   v%u x %.4f y %.4f z %.0f rgba %u %u %u %u stq %g %g %g uv %.4f %.4f\n", i,
                                v[i].x - cx.xyoffset.ofx / 16.0f, v[i].y - cx.xyoffset.ofy / 16.0f, v[i].z, v[i].r, v[i].g,
                                v[i].b, v[i].a, v[i].s, v[i].t, v[i].q, v[i].u / 16.0, v[i].v / 16.0);
                return;
            }
            float longest = 0.0f;
            for (int i = 0; i < 3; ++i)
            {
                const GSVertex &a = v[i], &b = v[(i + 1) % 3];
                longest = std::max(longest, std::hypot(a.x - b.x, a.y - b.y));
            }
            const float area = 0.5f * std::fabs((v[1].x - v[0].x) * (v[2].y - v[0].y) - (v[2].x - v[0].x) * (v[1].y - v[0].y));
            const float width = longest > 0.0f ? 2.0f * area / longest : 0.0f;
            // TTR_BENCH_CLAMPED=1: triangles with a vertex on the edge of the primitive space (0 or 4095) instead.
            static const bool clamped = std::getenv("TTR_BENCH_CLAMPED") != nullptr;
            if (clamped)
            {
                bool edge = false;
                for (int i = 0; i < 3; ++i)
                    edge |= v[i].x == 0.0f || v[i].x >= 4095.0f || v[i].y == 0.0f || v[i].y >= 4095.0f;
                if (!edge)
                    return;
            }
            else if (longest < 120.0f || width > 4.0f || area == 0.0f)
                return;
            const GSDrawState &st = batch.state;
            const GSContext &c = st.context;
            std::printf("prim %llu (#%llu in present) type %u tme %u abe %u fst %u fb %u tex %u/psm%u alpha %llx test %llx len %.0f width %.2f\n",
                        static_cast<unsigned long long>(index), static_cast<unsigned long long>(inPresent), st.prim.type,
                        st.prim.tme, st.prim.abe, st.prim.fst, c.frame.fbp, c.tex0.tbp0, c.tex0.psm,
                        static_cast<unsigned long long>(c.alpha), static_cast<unsigned long long>(c.test), longest, width);
            for (int i = 0; i < 3; ++i)
                std::printf("   v%d x %.4f y %.4f (raw %.4f %.4f) z %.0f rgba %u %u %u %u stq %g %g %g uv %u %u\n", i,
                            v[i].x - (c.xyoffset.ofx >> 4), v[i].y - (c.xyoffset.ofy >> 4), v[i].x, v[i].y, v[i].z, v[i].r, v[i].g, v[i].b,
                            v[i].a, v[i].s, v[i].t, v[i].q, v[i].u, v[i].v);
        };
        GSCapture::Replay(capture, nullptr, hooks);
        return 0;
    }

    if (options.touchingPage >= 0)
    {
        GSCapture::ReplayHooks hooks;
        hooks.maxPresents = options.maxPresents;
        uint64_t present = 0, index = 0;
        hooks.onPresent = [&](const GSPresentationRequest &, const PresentationFrame *) { ++present; };
        hooks.onSubmit = [&](const GSPrimitiveBatch &batch)
        {
            ++index;
            if (present < options.fromPresent)
                return;
            const GSDrawState &st = batch.state;
            const GSContext &c = st.context;
            const unsigned count = std::min<unsigned>(batch.vertexCount, 3u);
            float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
            for (unsigned i = 0; i < count; ++i)
            {
                minX = std::min(minX, batch.vertices[i].x);
                maxX = std::max(maxX, batch.vertices[i].x);
                minY = std::min(minY, batch.vertices[i].y);
                maxY = std::max(maxY, batch.vertices[i].y);
            }
            const int ofx = c.xyoffset.ofx >> 4, ofy = c.xyoffset.ofy >> 4;
            const int x0 = std::max<int>(static_cast<int>(std::floor(minX)) - ofx - 1, c.scissor.x0);
            const int x1 = std::min<int>(static_cast<int>(std::ceil(maxX)) - ofx + 1, c.scissor.x1);
            const int y0 = std::max<int>(static_cast<int>(std::floor(minY)) - ofy - 1, c.scissor.y0);
            const int y1 = std::min<int>(static_cast<int>(std::ceil(maxY)) - ofy + 1, c.scissor.y1);
            if (x0 > x1 || y0 > y1)
                return;
            std::vector<bool> fb(512), z(512), tex(512);
            pagesOfRect(fb, c.frame.fbp << 5u, c.frame.fbw, c.frame.psm, x0, y0, x1, y1);
            pagesOfRect(z, c.zbuf.zbp << 5u, c.frame.fbw, c.zbuf.psm, x0, y0, x1, y1);
            if (st.prim.tme)
                pagesOfRect(tex, c.tex0.tbp0, c.tex0.tbw, c.tex0.psm, 0, 0, std::max<int>(st.textureWidth, 1) - 1,
                            std::max<int>(st.textureHeight, 1) - 1);
            const int page = options.touchingPage;
            if (!fb[page] && !z[page] && !tex[page])
                return;
            std::printf("present %llu prim %llu %s%s%s type %u rect (%d,%d)-(%d,%d) fb %u/%u/psm%u msk %08x z %u/psm%u%s zte %u "
                        "tex %u/%u/psm%u %ux%u abe %u alpha %llx test %llx\n",
                        static_cast<unsigned long long>(present), static_cast<unsigned long long>(index), fb[page] ? "FB " : "",
                        z[page] ? "Z " : "", tex[page] ? "TEX " : "", st.prim.type, x0, y0, x1, y1, c.frame.fbp, c.frame.fbw,
                        c.frame.psm, c.frame.fbmsk, c.zbuf.zbp, c.zbuf.psm, c.zbuf.zmask ? " nowrite" : "",
                        static_cast<unsigned>((c.test >> 16u) & 1u), c.tex0.tbp0, c.tex0.tbw, c.tex0.psm, st.textureWidth,
                        st.textureHeight, st.prim.abe, static_cast<unsigned long long>(c.alpha),
                        static_cast<unsigned long long>(c.test));
        };
        GSCapture::Replay(capture, nullptr, hooks);
        return 0;
    }

    if (options.listPresents)
    {
        GSCapture::ReplayHooks hooks;
        uint64_t submits = 0;
        hooks.onSubmit = [&](const GSPrimitiveBatch &) { ++submits; };
        hooks.onPresent = [&](const GSPresentationRequest &r, const PresentationFrame *)
        {
            std::printf("vblank %llu prims %llu dispfb1 %llx dispfb2 %llx display1 %llx display2 %llx smode2 %llx pmode %llx "
                        "preferred %d fbp %u dest %u ctx fbp %u %u\n",
                        static_cast<unsigned long long>(r.vsyncTick), static_cast<unsigned long long>(submits),
                        static_cast<unsigned long long>(r.dispfb1), static_cast<unsigned long long>(r.dispfb2),
                        static_cast<unsigned long long>(r.display1), static_cast<unsigned long long>(r.display2),
                        static_cast<unsigned long long>(r.smode2), static_cast<unsigned long long>(r.pmode),
                        r.hasPreferredSource ? 1 : 0, r.preferredSource.fbp, r.preferredDestFbp, r.contextFrames[0].fbp,
                        r.contextFrames[1].fbp);
            submits = 0;
        };
        GSCapture::Replay(capture, nullptr, hooks);
        return 0;
    }

    if (options.stats)
    {
        std::map<std::string, ClassStats> classes;
        GSCapture::ReplayHooks hooks;
        hooks.maxPresents = options.maxPresents;
        double totalPixels = 0.0;
        hooks.onSubmit = [&](const GSPrimitiveBatch &batch)
        {
            double area = 0.0;
            ClassStats &entry = classes[classify(batch, area)];
            ++entry.primitives;
            entry.pixels += area;
            totalPixels += area;
        };
        const GSCapture::ReplayCounts counts = GSCapture::Replay(capture, nullptr, hooks);
        std::vector<std::pair<std::string, ClassStats>> sorted(classes.begin(), classes.end());
        std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b)
                  { return a.second.pixels > b.second.pixels; });
        std::printf("%llu primitives, %.1f M pixels (bounding boxes; triangles count half), %zu classes\n",
                    static_cast<unsigned long long>(counts.submits), totalPixels / 1e6, sorted.size());
        double cumulative = 0.0;
        for (size_t i = 0; i < sorted.size() && i < 40u; ++i)
        {
            cumulative += sorted[i].second.pixels;
            std::printf("%5.1f%% %5.1f%% %8llu prims  %s\n", 100.0 * sorted[i].second.pixels / totalPixels,
                        100.0 * cumulative / totalPixels, static_cast<unsigned long long>(sorted[i].second.primitives),
                        sorted[i].first.c_str());
        }
        return 0;
    }

    std::vector<uint64_t> expected;
    if (!options.checkHashesPath.empty())
    {
        std::vector<uint8_t> bytes;
        if (!readFile(options.checkHashesPath.c_str(), bytes))
        {
            std::printf("cannot read %s\n",options.checkHashesPath.c_str());
            return 2;
        }
        expected.resize(bytes.size() / 8u);
        std::memcpy(expected.data(), bytes.data(), expected.size() * 8u);
        if (options.hashesPath.empty())
            options.hashesPath = "-";
    }
    double best = 1e30;
    std::vector<uint64_t> hashes;
    std::vector<uint8_t> vram;
    GSCapture::ReplayCounts counts;
    uint64_t flips = 0;
    for (int run = 0; run < options.repeat; ++run)
    {
        vram = capture.vram;
        std::unique_ptr<GSRasterBackend> backend;
        GSGpuBackend *gpu = nullptr;
        if (options.gpu)
        {
            std::unique_ptr<GSGpuBackend> created = GSGpuBackend::Create(error);
            if (!created)
            {
                std::printf("no GPU backend: %s\n", error.c_str());
                return 1;
            }
            if (run == 0)
                std::printf("GPU: %s\n", created->Context().Renderer().c_str());
            created->LoadVram(capture.vram.data(), capture.vram.size());
            // Hashes include the presented frame: it must be this presentation's.
            created->SetSynchronousPresent(!options.hashesPath.empty() || !options.pngPrefix.empty() ||
                                           options.dumpAt != ~0ull);
            created->SetClutState(capture.clut);
            gpu = created.get();
            backend = std::move(created);
        }
        else
        {
            auto cpu = std::make_unique<GSCpuBackend>();
            cpu->Initialize(vram.data(), static_cast<uint32_t>(vram.size()));
            cpu->SetClutState(capture.clut);
            backend = std::move(cpu);
        }
        // VRAM after everything submitted so far (the GPU's is read back).
        auto currentVram = [&]()
        {
            backend->Flush();
            if (gpu)
                gpu->ReadVramAll(vram);
        };
        hashes.clear();
        flips = 0;
        // The game draws into a back buffer and copies it to the displayed one: a presentation shows a new
        // game frame when something was drawn into the displayed buffer since the previous one.
        uint32_t displayFbp = ~0u;
        bool drewDisplay = false;
        uint64_t presentIndex = 0;
        GSCapture::ReplayHooks hooks;
        hooks.maxPresents = options.maxPresents;
        hooks.onSubmit = [&](const GSPrimitiveBatch &batch)
        {
            if (batch.state.context.frame.fbp == displayFbp)
                drewDisplay = true;
        };
        hooks.onPresent = [&](const GSPresentationRequest &request, const PresentationFrame *frame)
        {
            if (drewDisplay)
                ++flips;
            drewDisplay = false;
            displayFbp = static_cast<uint32_t>(request.dispfb1 & 0x1FFu);
            if (!options.hashesPath.empty() || presentIndex == options.dumpAt)
            {
                currentVram();
                // VRAM after everything submitted so far, and the frame the backend presented.
                uint64_t hash = hashBytes(vram.data(), vram.size());
                if (frame && *frame)
                    hash ^= hashBytes(frame->pixels.data(), frame->pixels.size()) * 0x9E3779B97F4A7C15ull;
                hashes.push_back(hash);
                const size_t i = hashes.size() - 1u;
                if (presentIndex == options.dumpAt)
                    writeFile("at_" + std::to_string(presentIndex) + ".vram", vram.data(), vram.size());
                if (i < expected.size() && expected[i] != hashes.back())
                {
                    std::printf("presentation %zu differs: writing diff_%zu.vram\n",i, i);
                    writeFile("diff_" + std::to_string(i) + ".vram", vram.data(), vram.size());
                }
            }
            if (!options.pngPrefix.empty() && frame && *frame)
            {
                {
                    Image image{};
                    image.data = const_cast<uint8_t *>(frame->pixels.data());
                    image.width = static_cast<int>(frame->width);
                    image.height = static_cast<int>(frame->height);
                    image.mipmaps = 1;
                    image.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
                    char name[64];
                    std::snprintf(name, sizeof(name), "%04llu.png", static_cast<unsigned long long>(presentIndex));
                    ExportImage(image, (options.pngPrefix + name).c_str());
                }
            }
            ++presentIndex;
        };
        const double cpuStart = processCpuMs();
        const auto start = std::chrono::steady_clock::now();
        counts = GSCapture::Replay(capture, backend.get(), hooks);
        currentVram();
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        const double cpu = processCpuMs() - cpuStart;
        best = std::min(best, ms);
        // CPU time of every thread (spinning included): how much work the replay costs, whatever the waits.
        std::printf("run %d: %.1f ms, CPU %.1f ms (%.1f ms per game frame)\n", run + 1, ms, cpu,
                    cpu / static_cast<double>(std::max<uint64_t>(flips, 1u)));
        if (gpu)
        {
            const GSGpuBackend::Stats &s = gpu->GetStats();
            std::printf("GPU: %llu batches, %llu primitives, %llu tiles, %llu uploads, %llu CLUT loads, %llu fallbacks, "
                        "%llu snapshot pages\n",
                        static_cast<unsigned long long>(s.batches), static_cast<unsigned long long>(s.primitives),
                        static_cast<unsigned long long>(s.tiles), static_cast<unsigned long long>(s.uploads),
                        static_cast<unsigned long long>(s.clutLoads), static_cast<unsigned long long>(s.fallbacks),
                        static_cast<unsigned long long>(s.snapshotPages));
            std::printf("  primitives sampling pages but not blocks the batch draws: %llu\n", static_cast<unsigned long long>(s.sharedPages));
            if (s.submitMs > 0.0)
                std::printf("  CPU: submit %.1f ms (batches %.1f ms of it), uploads %.1f ms; waiting for the GPU %.1f ms "
                            "(after uploads %.1f ms)\n",
                            s.submitMs, s.drawMs, s.uploadMs, s.gpuWaitMs, s.uploadGpuMs);
            static const char *const names[8] = {"other", "read after write", "layout", "full", "CLUT", "upload over read",
                                                   "upload over write", "flush/sync"};
            for (int i = 0; i < 8; ++i)
                if (s.ended[i])
                    std::printf("  ended by %s: %llu\n", names[i], static_cast<unsigned long long>(s.ended[i]));
        }
    }

    const double frames = static_cast<double>(std::max<uint64_t>(flips, 1u));
    std::printf("%llu commands, %llu primitives, %llu uploads (%.1f MB), %llu presentations, %llu game frames%s\n",
                static_cast<unsigned long long>(counts.commands), static_cast<unsigned long long>(counts.submits),
                static_cast<unsigned long long>(counts.uploads), static_cast<double>(counts.uploadBytes) / 1048576.0,
                static_cast<unsigned long long>(counts.presents), static_cast<unsigned long long>(flips),
                counts.truncated ? " (capture truncated)" : "");
    std::printf("best %.1f ms: %.2f ms per game frame, %.2f M primitives/s\n", best, best / frames,
                static_cast<double>(counts.submits) / (best * 1000.0));
    std::printf("final VRAM hash %016llx\n", static_cast<unsigned long long>(hashBytes(vram.data(), vram.size())));
    if (!options.hashesPath.empty() && options.hashesPath != "-" &&
        !writeFile(options.hashesPath, hashes.data(), hashes.size() * sizeof(uint64_t)))
        std::printf("cannot write %s\n", options.hashesPath.c_str());
    if (!options.vramPath.empty() && !writeFile(options.vramPath, vram.data(), vram.size()))
        std::printf("cannot write %s\n", options.vramPath.c_str());
    return 0;
}
