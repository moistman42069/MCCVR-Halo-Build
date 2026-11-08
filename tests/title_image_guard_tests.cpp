// A PER-FRAME READER OF A TITLE'S IMAGE STOPS AT THE TITLE'S UNLOAD (src/dll/title_image_guard.h).
//
// The fault: Halo 3's pause reader can read halo3.dll+0xA3CA9A after MCC has freed the title at exit, because the
// worker's module poll is tens of milliseconds behind the loader and the render thread reads every frame. This test
// runs the production guard against a REAL DLL of this machine that is loaded and freed here (a small system DLL;
// nothing of MCC is touched): the address a reader keeps inside the image is retired by the loader's notification
// BEFORE the image is unmapped, and a reader thread hammering the address through the guard never faults.
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include "../src/dll/title_image_guard.h"

static int failures = 0;
static unsigned checks = 0;
static void Check(bool condition, const char* description)
{
    ++checks;
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", description);
        ++failures;
    }
}

// The reader's address inside the image (as g_nativePauseFlag is), and a copy nobody retires (the control).
static std::atomic<uintptr_t> g_watched{0};
static std::atomic<uintptr_t> g_unwatchedCopy{0};
static std::atomic<uint32_t> g_retired{0}, g_mappedAtCallback{0}, g_unmappedAtCallback{0};

static bool Mapped(uintptr_t address)
{
    MEMORY_BASIC_INFORMATION info{};
    return VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof(info)) == sizeof(info) &&
           info.State == MEM_COMMIT && !(info.Protect & (PAGE_NOACCESS | PAGE_GUARD));
}

// The production callback's shape (game.cpp, TitleImageUnloading): stores only.
static void OnUnload(uintptr_t base, size_t size)
{
    const uintptr_t address = g_watched.load(std::memory_order_acquire);
    if (!title_image_guard::Within(address, base, size))
        return;
    // The whole design rests on this: the notification arrives while the image is still mapped.
    if (Mapped(address))
        g_mappedAtCallback.fetch_add(1, std::memory_order_relaxed);
    else
        g_unmappedAtCallback.fetch_add(1, std::memory_order_relaxed);
    g_watched.store(0, std::memory_order_release);
    g_retired.fetch_add(1, std::memory_order_relaxed);
}

// The production readers' shape (ReadEnginePaused): load the address, read one byte, under SEH.
static bool ReadByteGuardedBody(std::atomic<uintptr_t>& slot, uint8_t& value, bool& faulted)
{
    const uintptr_t address = slot.load(std::memory_order_acquire);
    if (!address)
        return false;
    __try
    {
        value = *reinterpret_cast<const volatile uint8_t*>(address);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        faulted = true;
        return false;
    }
}
static bool ReadByte(std::atomic<uintptr_t>& slot, uint8_t& value, bool& faulted, bool guarded)
{
    if (guarded)
        title_image_guard::BeginRead();
    const bool known = ReadByteGuardedBody(slot, value, faulted);
    if (guarded)
        title_image_guard::EndRead();
    return known;
}

static std::atomic<bool> g_stop{false};
static std::atomic<uint64_t> g_guardedReads{0}, g_guardedFaults{0}, g_wrongBytes{0};
static DWORD WINAPI ReaderThread(LPVOID)
{
    while (!g_stop.load(std::memory_order_acquire))
    {
        uint8_t value = 0;
        bool faulted = false;
        if (ReadByte(g_watched, value, faulted, true))
        {
            g_guardedReads.fetch_add(1, std::memory_order_relaxed);
            if (value != 'M')
                g_wrongBytes.fetch_add(1, std::memory_order_relaxed);
        }
        if (faulted)
            g_guardedFaults.fetch_add(1, std::memory_order_relaxed);
    }
    return 0;
}

// A small system DLL this process has not loaded, that really unloads when freed.
static const wchar_t* PickDll()
{
    static const wchar_t* const candidates[]{L"lz32.dll", L"normaliz.dll", L"msimg32.dll", L"mpr.dll",
                                              L"wsock32.dll", L"avifil32.dll", L"msacm32.dll"};
    for (const wchar_t* name : candidates)
    {
        if (GetModuleHandleW(name))
            continue;
        const HMODULE module = LoadLibraryW(name);
        if (!module)
            continue;
        const bool freed = FreeLibrary(module) != 0;
        if (freed && !GetModuleHandleW(name))
            return name;
    }
    return nullptr;
}

int main()
{
    Check(!title_image_guard::Installed(), "not installed until asked");
    const wchar_t* name = PickDll();
    Check(name != nullptr, "a small system DLL that loads and unloads here");
    if (!name)
        return 1;

    // ---- CONTROL, before the guard exists: the address outlives the image, and reading it faults -----------
    {
        const HMODULE module = LoadLibraryW(name);
        Check(module != nullptr, "control: DLL loaded");
        const uintptr_t address = reinterpret_cast<uintptr_t>(module);   // its 'MZ'
        g_unwatchedCopy.store(address);
        uint8_t value = 0;
        bool faulted = false;
        Check(ReadByte(g_unwatchedCopy, value, faulted, false) && value == 'M' && !faulted,
              "control: the address reads while the DLL is loaded");
        Check(FreeLibrary(module) != 0 && !GetModuleHandleW(name), "control: DLL freed");
        Check(g_unwatchedCopy.load() == address && !Mapped(address),
              "control: nothing retired the address, and the image is gone");
        value = 0;
        Check(!ReadByte(g_unwatchedCopy, value, faulted, false) && faulted,
              "control: the read after unload faults (the fault MCC's exit left in the log)");
    }

    Check(title_image_guard::Install(&OnUnload) && title_image_guard::Installed(),
          "the loader's unload notification registers");
    Check(title_image_guard::Install(&OnUnload), "a second install is harmless");

    // ---- one unload, single-threaded ------------------------------------------------------------------------
    {
        const uint64_t unloadsBefore = title_image_guard::UnloadsSeen();
        const HMODULE module = LoadLibraryW(name);
        Check(module != nullptr, "DLL loaded");
        const uintptr_t address = reinterpret_cast<uintptr_t>(module);
        g_watched.store(address);
        uint8_t value = 0;
        bool faulted = false;
        Check(ReadByte(g_watched, value, faulted, true) && value == 'M', "the watched address reads while loaded");
        Check(FreeLibrary(module) != 0 && !GetModuleHandleW(name), "DLL freed");
        Check(g_retired.load() == 1 && g_watched.load() == 0, "the unload retired the address");
        Check(g_mappedAtCallback.load() == 1 && g_unmappedAtCallback.load() == 0,
              "the notification came while the image was still mapped");
        Check(!Mapped(address), "and the image is gone afterwards");
        Check(title_image_guard::UnloadsSeen() > unloadsBefore && title_image_guard::UnguardedUnloads() == 0,
              "the unload was seen, under the guard");
        value = 0;
        faulted = false;
        Check(!ReadByte(g_watched, value, faulted, true) && !faulted,
              "the reader reports unknown and touches nothing after the unload");
    }

    // ---- an unrelated DLL's unload retires nothing ------------------------------------------------------------
    {
        const HMODULE keep = LoadLibraryW(name);
        Check(keep != nullptr, "DLL loaded again");
        const uintptr_t address = reinterpret_cast<uintptr_t>(keep);
        g_watched.store(address);
        const uint32_t retiredBefore = g_retired.load();
        const wchar_t* other = nullptr;
        static const wchar_t* const others[]{L"msimg32.dll", L"normaliz.dll", L"lz32.dll", L"mpr.dll"};
        for (const wchar_t* candidate : others)
            if (lstrcmpiW(candidate, name) != 0 && !GetModuleHandleW(candidate))
            {
                other = candidate;
                break;
            }
        if (other)
        {
            const HMODULE second = LoadLibraryW(other);
            if (second)
            {
                const uint64_t seen = title_image_guard::UnloadsSeen();
                FreeLibrary(second);
                Check(title_image_guard::UnloadsSeen() > seen || GetModuleHandleW(other) != nullptr,
                      "another DLL's unload is seen");
            }
        }
        Check(g_retired.load() == retiredBefore && g_watched.load() == address,
              "an address in a DLL that stays loaded is not retired");
        Check(FreeLibrary(keep) != 0 && g_watched.load() == 0, "and is retired when its own DLL goes");
    }

    // ---- the race itself: a reader thread on the address while the DLL is loaded and freed, many times -----
    {
        HANDLE reader = CreateThread(nullptr, 0, ReaderThread, nullptr, 0, nullptr);
        Check(reader != nullptr, "reader thread started");
        const uint32_t retiredBefore = g_retired.load();
        constexpr int kCycles = 400;
        int cycles = 0;
        for (; cycles < kCycles; ++cycles)
        {
            const HMODULE module = LoadLibraryW(name);
            if (!module)
                break;
            g_watched.store(reinterpret_cast<uintptr_t>(module), std::memory_order_release);
            // Let the reader get onto the address before it goes.
            const uint64_t before = g_guardedReads.load(std::memory_order_relaxed);
            for (int spin = 0; spin < 200000 && g_guardedReads.load(std::memory_order_relaxed) < before + 50; ++spin)
                YieldProcessor();
            if (!FreeLibrary(module))
                break;
        }
        g_stop.store(true, std::memory_order_release);
        if (reader)
        {
            WaitForSingleObject(reader, 10000);
            CloseHandle(reader);
        }
        Check(cycles == kCycles, "every load/free cycle ran");
        Check(g_retired.load() - retiredBefore == static_cast<uint32_t>(kCycles), "every unload retired the address");
        Check(g_guardedReads.load() >= static_cast<uint64_t>(kCycles) * 50, "the reader was on the address throughout");
        Check(g_guardedFaults.load() == 0, "NO guarded read faulted across the unloads");
        Check(g_wrongBytes.load() == 0, "and every guarded read saw the image's own byte");
        Check(g_unmappedAtCallback.load() == 0, "the image was mapped at every notification");
        Check(title_image_guard::UnguardedUnloads() == 0, "every unload took the guard within its bound");
        std::printf("  %d load/free cycles under a reader: %llu guarded reads, %llu faults\n", kCycles,
                    static_cast<unsigned long long>(g_guardedReads.load()),
                    static_cast<unsigned long long>(g_guardedFaults.load()));
    }

    if (failures)
        return 1;
    std::printf("PASS: %u title image guard checks (a real DLL loaded and freed under a reader thread)\n", checks);
    return 0;
}
