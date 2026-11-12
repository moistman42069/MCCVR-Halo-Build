#include <windows.h>
#include <atomic>
#include "title_image_guard.h"

namespace
{
    // ntdll's loader notification (documented on MSDN as LdrRegisterDllNotification; not in the SDK headers).
    struct UnicodeString
    {
        USHORT Length, MaximumLength;
        PWSTR Buffer;
    };
    struct LdrDllNotificationData   // the LOADED and UNLOADED layouts are the same
    {
        ULONG Flags;
        const UnicodeString* FullDllName;
        const UnicodeString* BaseDllName;
        PVOID DllBase;
        ULONG SizeOfImage;
    };
    constexpr ULONG kLdrDllNotificationReasonUnloaded = 2;
    using LdrDllNotificationFn = VOID(CALLBACK*)(ULONG reason, const LdrDllNotificationData* data, PVOID context);
    using LdrRegisterDllNotificationFn =
        LONG(NTAPI*)(ULONG flags, LdrDllNotificationFn callback, PVOID context, PVOID* cookie);

    SRWLOCK g_guard = SRWLOCK_INIT;
    std::atomic<title_image_guard::UnloadFn> g_onUnload{nullptr};
    std::atomic<bool> g_installed{false};
    std::atomic<uint64_t> g_unloads{0}, g_unguardedUnloads{0};
    PVOID g_cookie = nullptr;
    using ShutdownInProgressFn = BOOLEAN(NTAPI*)();
    ShutdownInProgressFn g_shutdownInProgress = nullptr;   // ntdll!RtlDllShutdownInProgress

    VOID CALLBACK OnDllNotification(ULONG reason, const LdrDllNotificationData* data, PVOID)
    {
        if (reason != kLdrDllNotificationReasonUnloaded || !data || !data->DllBase || !data->SizeOfImage)
            return;
        // Loader lock held. Readers hold the guard for a handful of instructions; wait them out, then retire
        // every address in this image before the loader unmaps it.
        //
        // The wait is BOUNDED, and skipped while the process is shutting down: a reader thread that was killed
        // inside its read (ExitProcess ends every other thread wherever it stands) would hold the guard for
        // ever, and an unbounded wait here would then hang MCC's exit under the loader lock. Past the bound the
        // addresses are still cleared; a reader caught mid-read is back to what it had before this guard (its
        // own SEH).
        bool held = false;
        if (!g_shutdownInProgress || !g_shutdownInProgress())
            for (int spin = 0; spin < 4096 && !held; ++spin)
            {
                held = TryAcquireSRWLockExclusive(&g_guard) != 0;
                if (!held)
                {
                    if (spin < 256)
                        YieldProcessor();
                    else
                        SwitchToThread();
                }
            }
        if (const title_image_guard::UnloadFn onUnload = g_onUnload.load(std::memory_order_acquire))
            onUnload(reinterpret_cast<uintptr_t>(data->DllBase), data->SizeOfImage);
        g_unloads.fetch_add(1, std::memory_order_relaxed);
        if (held)
            ReleaseSRWLockExclusive(&g_guard);
        else
            g_unguardedUnloads.fetch_add(1, std::memory_order_relaxed);
    }
}

namespace title_image_guard
{
    bool Install(UnloadFn onUnload)
    {
        g_onUnload.store(onUnload, std::memory_order_release);
        bool expected = false;
        if (!g_installed.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            return true;
        const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        const auto registerNotification = ntdll
            ? reinterpret_cast<LdrRegisterDllNotificationFn>(GetProcAddress(ntdll, "LdrRegisterDllNotification"))
            : nullptr;
        g_shutdownInProgress = ntdll
            ? reinterpret_cast<ShutdownInProgressFn>(GetProcAddress(ntdll, "RtlDllShutdownInProgress"))
            : nullptr;
        if (!registerNotification || registerNotification(0, &OnDllNotification, nullptr, &g_cookie) != 0)
        {
            g_installed.store(false, std::memory_order_release);
            return false;
        }
        return true;
    }

    bool Installed() { return g_installed.load(std::memory_order_acquire); }

    void BeginRead() { AcquireSRWLockShared(&g_guard); }
    void EndRead() { ReleaseSRWLockShared(&g_guard); }

    uint64_t UnloadsSeen() { return g_unloads.load(std::memory_order_relaxed); }
    uint64_t UnguardedUnloads() { return g_unguardedUnloads.load(std::memory_order_relaxed); }
}
