// ttr_vu_bench <capture> [repeats]
//
// Replays VU1 microprograms captured from the game with PS2X_VU_CAPTURE=<file>:
// 1. runs every record on the fast engine and on the interpreter from the same state and reports the
//    records whose registers, flags, data memory or XGKICK packets differ;
// 2. times the fast engine (and the interpreter) over all records and prints ns per instruction pair.
#include "runtime/gs/gs_frontend.h"
#include "runtime/ps2_vu1.h"
#include "debug/sampling_profiler.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <tuple>
#include <vector>

namespace
{
    struct Record
    {
        VU1Interpreter::CaptureHeader header{};
        std::vector<uint8_t> code;
        std::vector<uint8_t> data;
        uint64_t codeKey = 0;
    };

    uint64_t fnv(const std::vector<uint8_t> &bytes)
    {
        uint64_t hash = 0xCBF29CE484222325ull;
        for (uint8_t b : bytes)
            hash = (hash ^ b) * 0x100000001B3ull;
        return hash;
    }

    struct Result
    {
        VU1State state{};
        std::vector<uint8_t> data;
        std::vector<std::vector<uint8_t>> kicks;
        int64_t pairs = 0;
    };

    Result runOne(VU1Interpreter &vu, GS &gs, Record &record, std::vector<uint8_t> &code, bool fast)
    {
        Result result;
        result.data = record.data;
        vu.state() = record.header.state;
        result.pairs = vu.runDetached(code.data(), record.header.codeSize, result.data.data(), record.header.dataSize, gs,
                                      record.codeKey, record.header.maxCycles, fast, &result.kicks);
        result.state = vu.state();
        return result;
    }

    bool same(const void *a, const void *b, size_t size) { return std::memcmp(a, b, size) == 0; }
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: ttr_vu_bench <capture> [repeats]\n");
        return 2;
    }
    const int repeats = argc > 2 ? std::atoi(argv[2]) : 5;
    ttr::debug::startSamplingProfilerFromEnvironment(); // TTR_PROFILE, as in the game

    FILE *file = std::fopen(argv[1], "rb");
    if (!file)
    {
        std::fprintf(stderr, "cannot open %s\n", argv[1]);
        return 1;
    }
    std::vector<Record> records;
    for (;;)
    {
        Record record;
        if (std::fread(&record.header, sizeof(record.header), 1, file) != 1)
            break;
        if (record.header.magic != 0x31435556u)
        {
            std::fprintf(stderr, "bad record %zu\n", records.size());
            return 1;
        }
        record.code.resize(record.header.codeSize);
        record.data.resize(record.header.dataSize);
        if (std::fread(record.code.data(), 1, record.code.size(), file) != record.code.size() ||
            std::fread(record.data.data(), 1, record.data.size(), file) != record.data.size())
            break;
        record.codeKey = fnv(record.code);
        records.push_back(std::move(record));
    }
    std::fclose(file);
    std::printf("%zu records\n", records.size());
    {
        size_t changes = 0;
        std::vector<uint64_t> keys;
        for (size_t i = 0; i < records.size(); ++i)
        {
            if (i == 0 || records[i].codeKey != records[i - 1].codeKey)
                ++changes;
            if (std::find(keys.begin(), keys.end(), records[i].codeKey) == keys.end())
                keys.push_back(records[i].codeKey);
        }
        std::printf("%zu code images, %zu changes between consecutive records\n", keys.size(), changes);
    }

    GS gs;
    VU1Interpreter fastVu(VU1Interpreter::Unit::VU1);
    VU1Interpreter refVu(VU1Interpreter::Unit::VU1);
    // Separate code buffers per engine: each one keys its decoded cache by buffer and code hash.
    std::vector<uint8_t> fastCode(16384), refCode(16384);

    // --deps: how much each run depends on the one before it (could consecutive runs execute in parallel?).
    // R: the registers it starts with replaced by those the previous run started with; M: the bytes the previous
    // run wrote (and VIF did not write again) replaced by what they held before it. Same = identical kicks and
    // data memory at the end.
    // --entries: pairs by code image and start PC.
    if (argc > 2 && std::strcmp(argv[2], "--entries") == 0)
    {
        std::vector<std::tuple<uint64_t, uint64_t, uint32_t, uint32_t>> rows; // pairs, key, pc, runs
        uint64_t all = 0;
        for (Record &record : records)
        {
            std::memcpy(fastCode.data(), record.code.data(), record.code.size());
            const Result r = runOne(fastVu, gs, record, fastCode, true);
            const uint64_t pairs = r.pairs > 0 ? static_cast<uint64_t>(r.pairs) : 0u;
            all += pairs;
            bool found = false;
            for (auto &row : rows)
                if (std::get<1>(row) == record.codeKey && std::get<2>(row) == record.header.state.pc)
                {
                    std::get<0>(row) += pairs;
                    ++std::get<3>(row);
                    found = true;
                }
            if (!found)
                rows.emplace_back(pairs, record.codeKey, record.header.state.pc, 1u);
        }
        std::sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) { return std::get<0>(a) > std::get<0>(b); });
        for (size_t i = 0; i < rows.size() && i < 25u; ++i)
            std::printf("%5.1f%%  image %016llx pc 0x%04x runs %u pairs/run %.0f\n", 100.0 * static_cast<double>(std::get<0>(rows[i])) / static_cast<double>(all),
                        static_cast<unsigned long long>(std::get<1>(rows[i])), std::get<2>(rows[i]), std::get<3>(rows[i]),
                        static_cast<double>(std::get<0>(rows[i])) / std::get<3>(rows[i]));
        return 0;
    }
    // --dump-code <record> <file>: the record's code image (for tools/vu_disasm.py) and its data memory (<file>.data).
    if (argc > 4 && std::strcmp(argv[2], "--dump-code") == 0)
    {
        const Record &record = records.at(std::strtoull(argv[3], nullptr, 10));
        const std::string path = argv[4];
        FILE *out = std::fopen(path.c_str(), "wb");
        FILE *data = std::fopen((path + ".data").c_str(), "wb");
        if (!out || !data)
            return 1;
        std::fwrite(record.code.data(), 1, record.code.size(), out);
        std::fwrite(record.data.data(), 1, record.data.size(), data);
        std::fclose(out);
        std::fclose(data);
        std::printf("record %s: pc 0x%04x, %zu code bytes\n", argv[3], record.header.state.pc, record.code.size());
        return 0;
    }
    // --find-xy <x> <y>: kicked qwords whose low 16 bits of words 0 and 1 are x and y (hex, 12.4 GS units), as
    // the reference interpreter produces them: record, image, start PC, kick and the whole qword.
    if (argc > 4 && std::strcmp(argv[2], "--find-xy") == 0)
    {
        const uint32_t wantX = static_cast<uint32_t>(std::strtoul(argv[3], nullptr, 16));
        const uint32_t wantY = static_cast<uint32_t>(std::strtoul(argv[4], nullptr, 16));
        size_t shown = 0;
        for (size_t i = 0; i < records.size() && shown < 40u; ++i)
        {
            Record &record = records[i];
            std::memcpy(refCode.data(), record.code.data(), record.code.size());
            const Result r = runOne(refVu, gs, record, refCode, false);
            for (size_t k = 0; k < r.kicks.size(); ++k)
            {
                const auto &kick = r.kicks[k];
                for (size_t q = 0; q + 16u <= kick.size(); q += 16u)
                {
                    uint32_t w[4];
                    std::memcpy(w, kick.data() + q, 16u);
                    if ((w[0] & 0xFFFFu) != wantX || (w[1] & 0xFFFFu) != wantY)
                        continue;
                    std::printf("record %zu image %016llx pc 0x%04x kick %zu qword %zu: %08x %08x %08x %08x\n", i,
                                static_cast<unsigned long long>(record.codeKey), record.header.state.pc, k, q / 16u, w[0], w[1],
                                w[2], w[3]);
                    ++shown;
                }
            }
        }
        return 0;
    }
    if (argc > 2 && std::strcmp(argv[2], "--deps") == 0)
    {
        size_t sameR = 0, sameM = 0, sameRM = 0, regsCarried = 0, resumed = 0, total = 0;
        Result previous;
        for (size_t i = 0; i < records.size(); ++i)
        {
            Record &record = records[i];
            std::memcpy(fastCode.data(), record.code.data(), record.code.size());
            const Result truth = runOne(fastVu, gs, record, fastCode, true);
            if (i > 0)
            {
                const Record &before = records[i - 1];
                ++total;
                const VU1State &s = record.header.state;
                if (same(s.vf, previous.state.vf, sizeof(s.vf)) && same(s.vi, previous.state.vi, sizeof(s.vi)))
                    ++regsCarried;
                if (s.pc == previous.state.pc)
                    ++resumed;
                auto variant = [&](bool regs, bool memory)
                {
                    Record copy = record;
                    if (regs)
                    {
                        const VU1State &old = before.header.state;
                        VU1State &st = copy.header.state;
                        std::memcpy(st.vf[1], old.vf[1], sizeof(st.vf) - sizeof(st.vf[0]));
                        std::memcpy(&st.vi[1], &old.vi[1], sizeof(st.vi) - sizeof(st.vi[0]));
                        std::memcpy(st.acc, old.acc, sizeof(st.acc));
                        st.q = old.q;
                        st.p = old.p;
                        st.i = old.i;
                        st.r = old.r;
                        st.mac = old.mac;
                        st.clip = old.clip;
                        st.status = old.status;
                    }
                    if (memory)
                        for (size_t b = 0; b < copy.data.size() && b < previous.data.size(); ++b)
                            if (previous.data[b] != before.data[b] && copy.data[b] == previous.data[b])
                                copy.data[b] = before.data[b];
                    const Result r = runOne(fastVu, gs, copy, fastCode, true);
                    return r.kicks == truth.kicks && r.data == truth.data;
                };
                sameR += variant(true, false);
                sameM += variant(false, true);
                sameRM += variant(true, true);
            }
            previous = truth;
        }
        std::printf("deps: %zu pairs of runs; start regs = previous end regs %zu; same pc %zu; same with R %zu, M %zu, R+M %zu\n",
                    total, regsCarried, resumed, sameR, sameM, sameRM);
        return 0;
    }

    // 1. correctness
    size_t mismatches = 0;
    uint64_t totalPairs = 0;
    for (size_t i = 0; i < records.size(); ++i)
    {
        Record &record = records[i];
        std::memcpy(fastCode.data(), record.code.data(), record.code.size());
        std::memcpy(refCode.data(), record.code.data(), record.code.size());
        const Result fast = runOne(fastVu, gs, record, fastCode, true);
        const Result ref = runOne(refVu, gs, record, refCode, false);
        if (fast.pairs < 0)
        {
            std::fprintf(stderr, "fast engine unavailable\n");
            return 1;
        }
        totalPairs += static_cast<uint64_t>(fast.pairs);
        const VU1State &a = fast.state, &b = ref.state;
        const bool regs = same(a.vf, b.vf, sizeof(a.vf)) && same(a.vi, b.vi, sizeof(a.vi)) && same(a.acc, b.acc, sizeof(a.acc)) &&
                          same(&a.q, &b.q, 4) && same(&a.p, &b.p, 4) && same(&a.i, &b.i, 4) && a.r == b.r && a.pc == b.pc;
        const bool flags = a.mac == b.mac && (!fastVu.fastMacStatusLive() || a.status == b.status) &&
                           (!fastVu.fastClipLive() || a.clip == b.clip);
        const bool dataSame = fast.data == ref.data;
        const bool kicksSame = fast.kicks == ref.kicks;
        // PS2X_BENCH_KICK_DIFF=1: how the kicked packets differ (words that differ, largest relative change).
        static const bool kickDiff = std::getenv("PS2X_BENCH_KICK_DIFF") != nullptr;
        if (kickDiff && !kicksSame && fast.kicks.size() == ref.kicks.size())
        {
            size_t words = 0, total = 0;
            double worst = 0.0;
            uint32_t worstA = 0, worstB = 0;
            for (size_t k = 0; k < fast.kicks.size(); ++k)
            {
                const auto &a = fast.kicks[k], &b = ref.kicks[k];
                for (size_t w = 0; w + 4 <= std::min(a.size(), b.size()); w += 4)
                {
                    uint32_t x = 0, y = 0;
                    std::memcpy(&x, a.data() + w, 4);
                    std::memcpy(&y, b.data() + w, 4);
                    ++total;
                    if (x == y)
                        continue;
                    ++words;
                    float fx = 0, fy = 0;
                    std::memcpy(&fx, &x, 4);
                    std::memcpy(&fy, &y, 4);
                    const double rel = std::fabs(static_cast<double>(fx) - fy) / std::max(1e-30, std::fabs(static_cast<double>(fy)));
                    if (rel > worst || worst == 0.0)
                    {
                        worst = rel;
                        worstA = x;
                        worstB = y;
                    }
                }
            }
            std::printf("record %zu: %zu of %zu kicked words differ, worst %08x vs %08x (rel %g)\n", i, words, total, worstA, worstB, worst);
        }
        if (!(regs && flags && dataSame && kicksSame))
        {
            if (mismatches < 20)
                std::printf("record %zu cycles %llu/%llu (start %llu) pc=0x%x: regs=%d flags=%d (mac %x/%x status %x/%x clip %x/%x) data=%d kicks=%d (%zu/%zu)\n", i,
                            static_cast<unsigned long long>(a.cycles), static_cast<unsigned long long>(b.cycles),
                            static_cast<unsigned long long>(record.header.state.cycles), record.header.state.pc, regs, flags, a.mac, b.mac, a.status, b.status, a.clip, b.clip, dataSame,
                            kicksSame, fast.kicks.size(), ref.kicks.size());
            ++mismatches;
        }
    }
    std::printf("correctness: %zu of %zu records differ; %llu pairs per pass\n", mismatches, records.size(),
                static_cast<unsigned long long>(totalPairs));
    // The interpreter counts a program's cycles differently (pipeline drain): compare this total between runs
    // of the fast engine instead (PS2X_VU_JIT=0, the plain loop, is the reference for the block compiler).
    std::printf("fast engine cycles: %llu\n", static_cast<unsigned long long>(fastVu.state().cycles));

    // 2. timing
    auto timeEngine = [&](bool fastEngine, int passes)
    {
        VU1Interpreter &vu = fastEngine ? fastVu : refVu;
        std::vector<uint8_t> &code = fastEngine ? fastCode : refCode;
        std::vector<uint8_t> data(16384);
        std::vector<std::vector<uint8_t>> kicks;
        uint64_t pairs = 0;
        double runSeconds = 0.0; // time inside runDetached only (not the copies that set up each record)
        const auto start = std::chrono::steady_clock::now();
        for (int pass = 0; pass < passes; ++pass)
        {
            for (Record &record : records)
            {
                if (std::memcmp(code.data(), record.code.data(), record.code.size()) != 0)
                    std::memcpy(code.data(), record.code.data(), record.code.size());
                std::memcpy(data.data(), record.data.data(), record.data.size());
                vu.state() = record.header.state;
                kicks.clear();
                const auto runStart = std::chrono::steady_clock::now();
                const int64_t n = vu.runDetached(code.data(), record.header.codeSize, data.data(), record.header.dataSize, gs,
                                                 record.codeKey, record.header.maxCycles, fastEngine, &kicks);
                runSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - runStart).count();
                pairs += n > 0 ? static_cast<uint64_t>(n) : 0u;
            }
        }
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::printf("%s: %.3f s in the engine of %.3f s\n", fastEngine ? "fast" : "interpreter", runSeconds, seconds);
        return std::make_pair(runSeconds, pairs);
    };

    const auto [fastSeconds, fastPairs] = timeEngine(true, repeats);
    std::printf("fast engine: %.3f s, %llu pairs, %.1f ns/pair, %.1f M pairs/s\n", fastSeconds,
                static_cast<unsigned long long>(fastPairs), fastSeconds * 1e9 / static_cast<double>(fastPairs),
                static_cast<double>(fastPairs) / fastSeconds / 1e6);
    const auto [refSeconds, ignored] = timeEngine(false, 1);
    (void)ignored;
    std::printf("interpreter: %.3f s for one pass, %.1f ns/pair\n", refSeconds,
                refSeconds * 1e9 / static_cast<double>(totalPairs));
    return mismatches == 0 ? 0 : 3;
}
