#include "debug/sampling_profiler.h"

#if defined(_WIN32)

#define NOMINMAX
#include <windows.h>
// windows.h must come first.
#include <dbghelp.h>
#include <tlhelp32.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace ttr::debug
{
    namespace
    {
        struct SampledThread
        {
            DWORD id = 0;
            HANDLE handle = nullptr;
        };

        std::vector<SampledThread> listOtherThreads(const std::vector<SampledThread> &previous)
        {
            std::vector<SampledThread> threads;
            const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
            if (snapshot == INVALID_HANDLE_VALUE)
                return previous;
            THREADENTRY32 entry{};
            entry.dwSize = sizeof(entry);
            const DWORD process = GetCurrentProcessId();
            const DWORD self = GetCurrentThreadId();
            for (BOOL ok = Thread32First(snapshot, &entry); ok; ok = Thread32Next(snapshot, &entry))
            {
                if (entry.th32OwnerProcessID != process || entry.th32ThreadID == self)
                    continue;
                const auto known = std::find_if(previous.begin(), previous.end(),
                                                [&](const SampledThread &t) { return t.id == entry.th32ThreadID; });
                if (known != previous.end())
                {
                    threads.push_back(*known);
                    continue;
                }
                const HANDLE handle = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
                                                 FALSE, entry.th32ThreadID);
                if (handle)
                    threads.push_back({entry.th32ThreadID, handle});
            }
            CloseHandle(snapshot);
            for (const SampledThread &old : previous)
            {
                const bool alive = std::any_of(threads.begin(), threads.end(),
                                               [&](const SampledThread &t) { return t.id == old.id; });
                if (!alive)
                    CloseHandle(old.handle);
            }
            return threads;
        }

        std::string symbolFor(DWORD64 address, std::unordered_map<DWORD64, std::string> &cache)
        {
            const auto cached = cache.find(address);
            if (cached != cache.end())
                return cached->second;
            alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME];
            auto *symbol = reinterpret_cast<SYMBOL_INFO *>(buffer);
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
            symbol->MaxNameLen = MAX_SYM_NAME;
            DWORD64 displacement = 0;
            std::string name = SymFromAddr(GetCurrentProcess(), address, &displacement, symbol)
                                   ? std::string(symbol->Name, symbol->NameLen)
                                   : "?";
            cache.emplace(address, name);
            return name;
        }

        using ThreadSamples = std::unordered_map<DWORD, std::unordered_map<DWORD64, uint64_t>>;

        void report(std::unordered_map<DWORD64, uint64_t> &samples, ThreadSamples &threadSamples,
                    std::unordered_map<DWORD64, std::string> &symbols, uint64_t total)
        {
            std::unordered_map<std::string, uint64_t> byFunction;
            for (const auto &[address, count] : samples)
                byFunction[symbolFor(address, symbols)] += count;
            std::vector<std::pair<std::string, uint64_t>> top(byFunction.begin(), byFunction.end());
            std::sort(top.begin(), top.end(), [](const auto &a, const auto &b) { return a.second > b.second; });

            std::cout << "[profile] " << total << " samples, top functions:" << std::endl;
            for (size_t i = 0; i < std::min<size_t>(top.size(), 15u); ++i)
            {
                std::cout << "[profile] " << (100.0 * static_cast<double>(top[i].second) / static_cast<double>(total))
                          << "% " << top[i].first << std::endl;
            }
            // Per thread: the share of samples outside the kernel's wait/sleep calls, and its top functions.
            for (auto &[thread, addresses] : threadSamples)
            {
                std::unordered_map<std::string, uint64_t> functions;
                uint64_t threadTotal = 0, busy = 0;
                for (const auto &[address, count] : addresses)
                {
                    const std::string name = symbolFor(address, symbols);
                    functions[name] += count;
                    threadTotal += count;
                    const bool waiting = name.rfind("Nt", 0) == 0 || name.rfind("Zw", 0) == 0;
                    if (!waiting)
                        busy += count;
                }
                if (threadTotal == 0 || busy * 20u < threadTotal)
                    continue; // under 5% busy
                std::vector<std::pair<std::string, uint64_t>> ranked(functions.begin(), functions.end());
                std::sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) { return a.second > b.second; });
                std::cout << "[profile] thread " << thread << " busy " << (100 * busy / threadTotal) << "%:";
                for (size_t i = 0; i < std::min<size_t>(ranked.size(), 8u); ++i)
                    std::cout << ' ' << ranked[i].first << '=' << (100 * ranked[i].second / threadTotal) << '%';
                std::cout << std::endl;
            }
            samples.clear();
            threadSamples.clear();
        }

        void profilerThread(int seconds)
        {
            SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
            SymInitialize(GetCurrentProcess(), nullptr, TRUE);
            std::unordered_map<DWORD64, uint64_t> samples;
            ThreadSamples threadSamples;
            std::unordered_map<DWORD64, std::string> symbols;
            std::vector<SampledThread> threads;
            uint64_t total = 0;
            auto nextList = std::chrono::steady_clock::now();
            auto nextReport = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
            for (;;)
            {
                const auto now = std::chrono::steady_clock::now();
                if (now >= nextList)
                {
                    threads = listOtherThreads(threads);
                    nextList = now + std::chrono::seconds(1);
                }
                for (const SampledThread &thread : threads)
                {
                    if (SuspendThread(thread.handle) == static_cast<DWORD>(-1))
                        continue;
                    CONTEXT context{};
                    context.ContextFlags = CONTEXT_CONTROL;
                    if (GetThreadContext(thread.handle, &context))
                    {
                        ++samples[context.Rip];
                        ++threadSamples[thread.id][context.Rip];
                        ++total;
                    }
                    ResumeThread(thread.handle);
                }
                if (now >= nextReport)
                {
                    report(samples, threadSamples, symbols, total);
                    total = 0;
                    nextReport = now + std::chrono::seconds(seconds);
                }
                Sleep(1);
            }
        }
    }

    void startSamplingProfilerFromEnvironment()
    {
        const char *env = std::getenv("TTR_PROFILE");
        if (!env)
            return;
        const int seconds = std::max(1, std::atoi(env));
        std::thread(profilerThread, seconds).detach();
        std::cout << "[profile] sampling every 1 ms, report every " << seconds << " s" << std::endl;
    }
}

#else

namespace ttr::debug
{
    void startSamplingProfilerFromEnvironment()
    {
    }
}

#endif
