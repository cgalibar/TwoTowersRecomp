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
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

        // One sample: the instruction pointer, then the return addresses of the callers (innermost first).
        constexpr size_t kMaxFrames = 48;
        struct Stack
        {
            DWORD64 frames[kMaxFrames];
            uint32_t count = 0;
        };

        struct ThreadProfile
        {
            std::unordered_map<DWORD64, uint64_t> self;      // by instruction pointer
            std::unordered_map<DWORD64, uint64_t> inclusive; // by function start (or the address when unknown)
            // Samples by (innermost frame, its caller), to tell who calls memmove and friends.
            std::unordered_map<uint64_t, uint64_t> pairCounts;
            std::vector<std::pair<DWORD64, DWORD64>> pairs;
            uint64_t total = 0;
        };

        uint64_t pairKey(DWORD64 leaf, DWORD64 caller)
        {
            return (leaf * 0x9E3779B97F4A7C15ull) ^ caller;
        }

        bool readReturnAddress(DWORD64 stackPointer, DWORD64 &value)
        {
            __try
            {
                value = *reinterpret_cast<const DWORD64 *>(stackPointer);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        bool unwindFrame(DWORD64 imageBase, PRUNTIME_FUNCTION function, CONTEXT &context)
        {
            __try
            {
                void *handlerData = nullptr;
                DWORD64 establisher = 0;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, function, &context, &handlerData,
                                 &establisher, nullptr);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        // Walks the stack of a suspended thread. It only reads memory: no allocation and none of our locks.
        void captureStack(const CONTEXT &initial, Stack &stack)
        {
            CONTEXT context = initial;
            stack.count = 0;
            while (stack.count < kMaxFrames && context.Rip != 0)
            {
                stack.frames[stack.count++] = context.Rip;
                DWORD64 imageBase = 0;
                const PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &imageBase, nullptr);
                if (!function)
                {
                    // A leaf function or generated code: only the innermost frame can be stepped over this way.
                    DWORD64 returnAddress = 0;
                    if (stack.count != 1 || !readReturnAddress(context.Rsp, returnAddress))
                        break;
                    if (!RtlLookupFunctionEntry(returnAddress, &imageBase, nullptr))
                        break; // not a return address (generated code keeps its own frame)
                    context.Rip = returnAddress;
                    context.Rsp += 8;
                    continue;
                }
                if (!unwindFrame(imageBase, function, context))
                    break;
            }
        }

        struct Symbol
        {
            std::string name;
        };

        const std::string &symbolFor(DWORD64 address, std::unordered_map<DWORD64, Symbol> &cache)
        {
            const auto cached = cache.find(address);
            if (cached != cache.end())
                return cached->second.name;
            alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME];
            auto *symbol = reinterpret_cast<SYMBOL_INFO *>(buffer);
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
            symbol->MaxNameLen = MAX_SYM_NAME;
            DWORD64 displacement = 0;
            Symbol result;
            result.name = SymFromAddr(GetCurrentProcess(), address, &displacement, symbol)
                              ? std::string(symbol->Name, symbol->NameLen)
                              : "?";
            return cache.emplace(address, std::move(result)).first->second.name;
        }

        bool isWait(const std::string &name)
        {
            return name.rfind("Nt", 0) == 0 || name.rfind("Zw", 0) == 0;
        }

        // Functions whose own time says little: their samples are also listed by caller.
        bool isSharedLeaf(const std::string &name)
        {
            return name == "memmove" || name == "memcpy" || name == "memset" || name == "?" ||
                   name == "RtlQueryPerformanceCounter" || name == "malloc_base" || name == "free_base" ||
                   name == "RtlSizeHeap" || name == "RtlFreeHeap" || name == "RtlAllocateHeap" ||
                   name == "NtFreeVirtualMemory" || name == "NtAllocateVirtualMemory";
        }

        std::string percent(uint64_t part, uint64_t whole)
        {
            char text[32];
            std::snprintf(text, sizeof(text), "%5.1f%%", whole ? 100.0 * double(part) / double(whole) : 0.0);
            return text;
        }

        std::vector<std::pair<std::string, uint64_t>> ranked(const std::unordered_map<std::string, uint64_t> &map)
        {
            std::vector<std::pair<std::string, uint64_t>> list(map.begin(), map.end());
            std::sort(list.begin(), list.end(), [](const auto &a, const auto &b) { return a.second > b.second; });
            return list;
        }

        void report(std::unordered_map<DWORD, ThreadProfile> &profiles, std::unordered_map<DWORD64, Symbol> &symbols,
                    size_t threadFunctions)
        {
            uint64_t total = 0;
            for (const auto &[thread, profile] : profiles)
                total += profile.total;
            std::cout << "[profile] " << total << " samples" << std::endl;
            // Per thread: the share of samples outside the kernel's wait/sleep calls, then its top functions by
            // own time ("self") and by inclusive time ("incl": the function or anything it calls).
            for (auto &[thread, profile] : profiles)
            {
                std::unordered_map<std::string, uint64_t> self;
                uint64_t busy = 0;
                for (const auto &[address, count] : profile.self)
                {
                    const std::string &name = symbolFor(address, symbols);
                    self[name] += count;
                    if (!isWait(name))
                        busy += count;
                }
                if (profile.total == 0 || busy * 20u < profile.total)
                    continue; // under 5% busy
                std::unordered_map<std::string, uint64_t> inclusive;
                for (const auto &[address, count] : profile.inclusive)
                    inclusive[symbolFor(address, symbols)] += count;
                std::unordered_map<std::string, uint64_t> callers;
                for (const auto &[leaf, caller] : profile.pairs)
                {
                    const std::string &leafName = symbolFor(leaf, symbols);
                    if (!isSharedLeaf(leafName))
                        continue;
                    callers[leafName + " <- " + symbolFor(caller, symbols)] += profile.pairCounts[pairKey(leaf, caller)];
                }

                std::cout << "[profile] thread " << thread << " busy " << percent(busy, profile.total) << " of "
                          << profile.total << " samples" << std::endl;
                const auto selfRanked = ranked(self);
                for (size_t i = 0; i < std::min(selfRanked.size(), threadFunctions); ++i)
                {
                    std::cout << "[profile]   self " << percent(selfRanked[i].second, profile.total) << "  incl "
                              << percent(inclusive[selfRanked[i].first], profile.total) << "  " << selfRanked[i].first
                              << std::endl;
                }
                const auto inclusiveRanked = ranked(inclusive);
                size_t printed = 0;
                for (size_t i = 0; i < inclusiveRanked.size() && printed < threadFunctions; ++i)
                {
                    if (isWait(inclusiveRanked[i].first))
                        continue;
                    std::cout << "[profile]   incl " << percent(inclusiveRanked[i].second, profile.total) << "  "
                              << inclusiveRanked[i].first << std::endl;
                    ++printed;
                }
                const auto callerRanked = ranked(callers);
                for (size_t i = 0; i < std::min<size_t>(callerRanked.size(), 24u); ++i)
                {
                    std::cout << "[profile]   from " << percent(callerRanked[i].second, profile.total) << "  "
                              << callerRanked[i].first << std::endl;
                }
            }
            profiles.clear();
        }

        void profilerThread(int seconds, size_t threadFunctions)
        {
            SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
            SymInitialize(GetCurrentProcess(), nullptr, TRUE);
            std::unordered_map<DWORD, ThreadProfile> profiles;
            std::unordered_map<DWORD64, Symbol> symbols;
            std::unordered_map<DWORD64, DWORD64> functionStarts; // code address -> function start
            std::vector<SampledThread> threads;
            auto nextList = std::chrono::steady_clock::now();
            auto nextReport = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
            Stack stack;
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
                    context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
                    const bool sampled = GetThreadContext(thread.handle, &context) != FALSE;
                    if (sampled)
                        captureStack(context, stack);
                    // Resume before touching the maps: they allocate, and the suspended thread may hold the
                    // heap lock (that deadlocked the whole process after a few minutes).
                    ResumeThread(thread.handle);
                    if (!sampled || stack.count == 0)
                        continue;
                    ThreadProfile &profile = profiles[thread.id];
                    ++profile.self[stack.frames[0]];
                    ++profile.total;
                    // Inclusive time: once per distinct function on the stack.
                    DWORD64 seen[kMaxFrames];
                    size_t seenCount = 0;
                    for (uint32_t i = 0; i < stack.count; ++i)
                    {
                        const DWORD64 address = stack.frames[i];
                        auto known = functionStarts.find(address);
                        if (known == functionStarts.end())
                        {
                            DWORD64 imageBase = 0;
                            const PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(address, &imageBase, nullptr);
                            // Functions split into several unwind ranges are merged later by symbol name.
                            known = functionStarts.emplace(address, function ? imageBase + function->BeginAddress : address).first;
                        }
                        const DWORD64 start = known->second;
                        if (std::find(seen, seen + seenCount, start) != seen + seenCount)
                            continue;
                        seen[seenCount++] = start;
                        ++profile.inclusive[start];
                    }
                    if (stack.count > 1)
                    {
                        if (++profile.pairCounts[pairKey(stack.frames[0], stack.frames[1])] == 1)
                            profile.pairs.emplace_back(stack.frames[0], stack.frames[1]);
                    }
                }
                if (now >= nextReport)
                {
                    report(profiles, symbols, threadFunctions);
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
        const char *comma = std::strchr(env, ',');
        const size_t threadFunctions = comma ? static_cast<size_t>(std::max(1, std::atoi(comma + 1))) : 8u;
        std::thread(profilerThread, seconds, threadFunctions).detach();
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
