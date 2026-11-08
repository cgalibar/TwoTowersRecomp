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
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
        if (!(regs && flags && dataSame && kicksSame))
        {
            if (mismatches < 20)
                std::printf("record %zu pc=0x%x: regs=%d flags=%d (mac %x/%x status %x/%x clip %x/%x) data=%d kicks=%d (%zu/%zu)\n", i,
                            record.header.state.pc, regs, flags, a.mac, b.mac, a.status, b.status, a.clip, b.clip, dataSame,
                            kicksSame, fast.kicks.size(), ref.kicks.size());
            ++mismatches;
        }
    }
    std::printf("correctness: %zu of %zu records differ; %llu pairs per pass\n", mismatches, records.size(),
                static_cast<unsigned long long>(totalPairs));

    // 2. timing
    auto timeEngine = [&](bool fastEngine, int passes)
    {
        VU1Interpreter &vu = fastEngine ? fastVu : refVu;
        std::vector<uint8_t> &code = fastEngine ? fastCode : refCode;
        std::vector<uint8_t> data(16384);
        std::vector<std::vector<uint8_t>> kicks;
        uint64_t pairs = 0;
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
                const int64_t n = vu.runDetached(code.data(), record.header.codeSize, data.data(), record.header.dataSize, gs,
                                                 record.codeKey, record.header.maxCycles, fastEngine, &kicks);
                pairs += n > 0 ? static_cast<uint64_t>(n) : 0u;
            }
        }
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        return std::make_pair(seconds, pairs);
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
