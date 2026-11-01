#pragma once
#include <cstddef>
#include <cstdint>

// A READ OF A TITLE'S IMAGE THAT CANNOT OUTLIVE THE IMAGE.
//
// Several per-frame readers keep an address INSIDE a title DLL: Halo 3's native pause flag, ODST's, Reach's, and
// Halo 4's pause getter. The worker learns that a title has gone on its next module poll, tens of milliseconds
// later; the render thread reads every frame. When MCC exits it frees the title DLL while frames are still being
// presented, so an unguarded reader dereferences an unmapped image: one caught first-chance fault per exit, in
// HaloMCCVR-native-faults.log (Halo 3: a read of halo3.dll+0xA3CA9A from the pause reader).
// A poll cannot close that window however fast it is.
//
// The loader can. LdrRegisterDllNotification calls us when a DLL is being unloaded, AFTER its DllMain has
// detached and BEFORE its image is unmapped. The callback takes this guard exclusively and clears every address
// that lies in the departing image; a reader holds the guard shared from the moment it loads its address to the
// moment it has finished reading. So a reader that saw a non-zero address finishes while the image is still
// mapped (the loader waits for it), and every later reader sees zero. No read happens after the DLL is gone.
//
// Rules:
//   * the callback runs under the loader lock: atomic stores only, no logging, no allocation, no loader calls;
//   * a reader must not call anything that takes the loader lock between BeginRead and EndRead;
//   * BeginRead/EndRead are plain functions on purpose (the readers use __try, which forbids destructors).
namespace title_image_guard
{
    // Called with the guard held exclusively for each DLL that is about to be unmapped.
    using UnloadFn = void (*)(uintptr_t base, size_t size);

    // Once, at startup. Returns false when the loader's notification could not be registered: readers then
    // behave exactly as before (the worker's poll, and their own SEH).
    bool Install(UnloadFn onUnload);
    bool Installed();

    void BeginRead();
    void EndRead();

    // How many DLL unloads the callback has seen (any DLL), for the log and the tests.
    uint64_t UnloadsSeen();
    // How many of them could not take the guard within its bound (a reader stuck or killed mid-read).
    uint64_t UnguardedUnloads();

    inline bool Within(uintptr_t address, uintptr_t base, size_t size)
    {
        return address >= base && address - base < size;
    }
}
