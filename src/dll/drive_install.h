#pragma once
// WHAT AN INSTALL DECIDES ONCE ITS DRIVE IS RESOLVED. A drive's plan (src/common/drive_sites.h) says where
// everything is; before a hook exists the install proves a little more on the LOADED module itself: each address
// is on a page of committed code, each call is read again and lands where it must, the dispatcher's frame comes
// out of the system's own function table and is the one the plan proved from the module's .pdata, the engine TLS
// index is the one the lane's run-time readers use. Those decisions live here, in one place, so that the DLL's
// installs and the offline check (tools/drive_address_check.cpp, on each game DLL as the loader maps it) run the
// SAME code: what the tool prints as HOOK is what the install would do.
//
// Nothing here hooks, logs or writes: each function returns what was proven, and the caller says it.
//
// NO READ IN THIS FILE MAY FAULT. The plan reads the module through checks (drive_resolve.h: Readable), so a page
// that cannot be read costs it one line and it returns normally -- with that line refused and everything else it
// found still set. What runs after the plan runs on the cold worker with no guard around it. So every read of
// module memory in the functions below is guarded where it is made, and nothing the plan refused is read again.
//
// That is this file's rule and covers these functions only. An install binds more than they decide, in its own
// code and after them -- the seat frame reader (seat_frame_reader.h: Bind), the occupant's aim vector
// (legacy_direct_drive.inl: LegacyDriveBindUnitAim) -- and those read the module with no guard of their own, as
// they did before these decisions were gathered here.
#include <windows.h>

#include <cstdio>
#include <cstring>

#include "../common/aim_caller_logic.h"
#include "../common/drive_sites.h"

namespace drive_install
{
// `address` is on a page of committed code.
inline bool Executable(uintptr_t address)
{
    MEMORY_BASIC_INFORMATION info{};
    if (!address || !VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof(info)) ||
        info.State != MEM_COMMIT || (info.Protect & PAGE_GUARD))
        return false;
    const DWORD protection = info.Protect & 0xFFu;
    return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ || protection == PAGE_EXECUTE_READWRITE ||
           protection == PAGE_EXECUTE_WRITECOPY;
}

// The dispatcher-frame proof on the loaded module: the entry of the system's function table that contains the
// dispatcher, then the engine-free proof over it (aim_caller::ProveDispatcherFrame: the entry begins at the
// dispatcher and contains the call past the prolog; the unwind info and the prologue's own instructions decode
// to one fixed frame). `entryFrame`: where the dispatcher's return sits relative to the called function's entry
// rsp. `why`: the first failing proof.
// It reads the function's table entry, its unwind info and the dispatcher's first bytes: under a guard, so a
// page of them that cannot be read is a failed proof and not a fault.
inline bool ProveDispatcher(uintptr_t base, size_t size, uint32_t dispatcherRva, uint32_t callRva, uint32_t& entryFrame,
                            const char*& why)
{
    entryFrame = 0;
    why = nullptr;
    __try
    {
        DWORD64 imageBase = 0;
        const PRUNTIME_FUNCTION function =
            RtlLookupFunctionEntry(static_cast<DWORD64>(base + dispatcherRva), &imageBase, nullptr);
        if (!function || imageBase != static_cast<DWORD64>(base))
        {
            why = "dispatcher .pdata entry";
            return false;
        }
        return aim_caller::ProveDispatcherFrame(reinterpret_cast<const uint8_t*>(base), size, dispatcherRva,
                                               function->BeginAddress, function->EndAddress, function->UnwindData,
                                               callRva, entryFrame, why);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        entryFrame = 0;
        why = "the dispatcher's bytes or unwind info could not be read";
        return false;
    }
}

// A read of the loaded module that must not fault the install: false when the bytes cannot be read.
inline bool Read(uintptr_t address, void* out, size_t size)
{
    __try
    {
        std::memcpy(out, reinterpret_cast<const void*>(address), size);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// The `call` (E8 rel32) at `callRva` lands on `targetRva`: its return address, or 0.
inline uintptr_t CallReturn(uintptr_t base, size_t size, uint32_t callRva, uint32_t targetRva)
{
    if (!callRva || static_cast<size_t>(callRva) + 5 > size)
        return 0;
    uint8_t call[5];
    if (!Read(base + callRva, call, sizeof(call)))
        return 0;
    int32_t rel = 0;
    std::memcpy(&rel, call + 1, sizeof(rel));
    if (call[0] != 0xE8 || static_cast<int64_t>(callRva) + 5 + rel != static_cast<int64_t>(targetRva))
        return 0;
    return base + callRva + 5;
}

// ------------------------------------------------------------------------------------------ Reach control update
struct ReachControlDecision
{
    bool entryOk = false, tlsOk = false, accOk = false, indexOk = false;
    bool hook = false;
};

// `readersTlsIndexRva`: where this lane's run-time readers read the engine TLS index. The module's own (which the
// plan proved the control update loads) must be that one: a build whose index moved is NOT followed.
inline ReachControlDecision DecideReachControl(uintptr_t base, bool opened, const drive_sites::ReachControl& found,
                                               uint32_t readersTlsIndexRva)
{
    ReachControlDecision d;
    d.entryOk = opened && found.entry && Executable(base + found.entry);
    d.tlsOk = opened && found.tlsBlock && Executable(base + found.tlsBlock);
    d.accOk = opened && found.accumulator && Executable(base + found.accumulator);
    d.indexOk = opened && found.tlsIndex && found.tlsIndex == readersTlsIndexRva;
    d.hook = d.entryOk && d.tlsOk && d.accOk && d.indexOk;
    return d;
}

// ------------------------------------------------------------------------------ Halo 3 / ODST / Halo 4 control update
inline bool DecideLegacyControl(bool opened, const drive_sites::LegacyControl& found)
{
    return opened && found.ok;
}

// ----------------------------------------------------------------------------------------------- the caller map
struct CallerMapProof
{
    bool proven = false;
    const char* why = nullptr;                         // not proven: the first proof that failed
    uintptr_t ret[drive_sites::kMaxCallers]{};         // each site's return address (0: not verified, reads unknown)
};

// WHICH CALLER RAN THE CONTROL UPDATE, proven on the loaded module from the calls the plan found: each call is
// read again and must land on its target; in dispatcher mode the frame is taken from the system's function table
// and must be the one the plan proved. Not proven: the map is cleared, and every call counts as the update.
inline CallerMapProof BuildCallerMap(uintptr_t base, size_t size, uint32_t entryRva, const drive_sites::Callers& found,
                                     const drive_sites::CallerAnchors& anchors, aim_caller::CallerMap& map)
{
    CallerMapProof proof;
    map.clear();
    proof.why = found.why;
    uint32_t callTarget = entryRva;
    if (anchors.accumulatorCall.pattern)
    {
        // DISPATCHER MODE: the control update's one call site, the dispatcher around it, and its frame.
        const uintptr_t accRet = CallReturn(base, size, found.accumulatorCall, entryRva);
        uint32_t frame = 0;
        if (!accRet)
            proof.why = proof.why ? proof.why : "the accumulator's call site";
        else if (!found.dispatcher)
            proof.why = proof.why ? proof.why : "the dispatcher prologue";
        else if (!found.frame)   // the plan refused the frame: what it could not read is not read again here
            proof.why = proof.why ? proof.why : "the dispatcher's frame, which the plan did not prove";
        else if (ProveDispatcher(base, size, found.dispatcher, found.accumulatorCall, frame, proof.why) &&
                 frame != found.frame)
        {
            frame = 0;
            proof.why = "the dispatcher's frame differs between the loaded function table and the module's .pdata";
        }
        if (!frame)
        {
            proof.why = proof.why ? proof.why : "dispatcher frame";
            return proof;
        }
        map.acc_ret = accRet;
        map.dispatcher_frame = frame;
        callTarget = found.dispatcher;
    }
    map.no_unit_action = anchors.noUnitAction;
    for (int i = 0; i < drive_sites::kMaxCallers; ++i)
    {
        const drive_sites::CallSite& site = anchors.sites[i];
        if (!site.pattern)
            continue;
        const uintptr_t ret = CallReturn(base, size, found.call[i], callTarget);
        proof.ret[i] = ret && map.add(ret, site.role) ? ret : 0;
    }
    if (!map.stick_proven())
    {
        // What actually failed: the plan's own reason when it refused the roles (two at one call, or out of
        // order), else the stick update's call.
        map.clear();
        proof.why = found.why ? found.why : "the stick update's call";
        return proof;
    }
    proof.proven = true;
    return proof;
}

// The sites of a caller map as its log line names them: "stick +0x..., impulse NOT verified (reads unknown)".
inline void CallerMapText(uintptr_t base, const drive_sites::CallerAnchors& anchors, const CallerMapProof& proof,
                          char* out, size_t size)
{
    size_t used = 0;
    if (size)
        out[0] = 0;
    for (int i = 0; i < drive_sites::kMaxCallers; ++i)
    {
        const drive_sites::CallSite& site = anchors.sites[i];
        if (!site.pattern)
            continue;
        const int n = proof.ret[i]
            ? std::snprintf(out + used, size - used, "%s%s +0x%llX", used ? ", " : "", aim_caller::CallerName(site.role),
                            static_cast<unsigned long long>(proof.ret[i] - base))
            : std::snprintf(out + used, size - used, "%s%s NOT verified (reads unknown)", used ? ", " : "",
                            aim_caller::CallerName(site.role));
        if (n > 0)
            used = used + static_cast<size_t>(n) < size - 1 ? used + static_cast<size_t>(n) : size - 1;
    }
}

// ------------------------------------------------------------------------------------- a pilot's controller
struct FlightDecision
{
    bool rowsOk = false;    // every row found (and, where asked, on a page of code)
    bool tlsOk = false;     // the controller's TLS index is the one the lane's record is read through
    bool callOk = false;    // the dispatcher's call lands on the controller
    bool frameOk = false;   // the dispatcher's frame, from the system's table, is the plan's
    bool hook = false;
    uint32_t frame = 0;
    const char* frameWhy = nullptr;
    uintptr_t callerRet[drive_sites::kMaxCallers]{};
    char failed[96]{};      // the rows that failed, by name
};

// `tlsIndexAddress`: where the lane this controller writes through reads the engine TLS index (Reach: its
// run-time readers' constant; Halo 4: what the control update's own drive resolved). `rowsOnCode`: each row must
// also be on a page of committed code. `callers`: how many of the dispatcher's callers the drive names; `stick`:
// which of them is the stick update, the one that must be proven.
template <size_t N>
inline FlightDecision DecideFlight(uintptr_t base, size_t size, bool opened, const drive_sites::Anchor (&rows)[N],
                                   const uint32_t (&rowsFound)[N], const drive_sites::Flight& found,
                                   uintptr_t tlsIndexAddress, bool rowsOnCode, int callers, int stick)
{
    FlightDecision d;
    size_t used = 0;
    d.rowsOk = opened;
    for (size_t i = 0; i < N; ++i)
    {
        if (rowsFound[i] && (!rowsOnCode || Executable(base + rowsFound[i])))
            continue;
        d.rowsOk = false;
        const int n = std::snprintf(d.failed + used, sizeof(d.failed) - used, "%s%s", used ? "," : "", rows[i].name);
        if (n > 0 && used + static_cast<size_t>(n) < sizeof(d.failed))
            used += static_cast<size_t>(n);
    }
    d.tlsOk = opened && found.tlsIndex && tlsIndexAddress && base + found.tlsIndex == tlsIndexAddress;
    d.callOk = d.rowsOk && CallReturn(base, size, found.call, found.entry) != 0;
    d.frameWhy = found.frameWhy;
    // A frame the plan refused is not proven again here: what it could not read is not read again.
    d.frameOk = d.callOk && d.tlsOk && found.dispatcher && found.frame &&
                ProveDispatcher(base, size, found.dispatcher, found.call, d.frame, d.frameWhy) && d.frame &&
                d.frame == found.frame;
    for (int i = 0; d.frameOk && i < callers && i < drive_sites::kMaxCallers; ++i)
        d.callerRet[i] = CallReturn(base, size, found.callerCall[i], found.dispatcher);
    d.hook = d.rowsOk && d.tlsOk && d.callOk && d.frameOk && stick >= 0 && stick < drive_sites::kMaxCallers &&
             d.callerRet[stick] != 0;
    return d;
}

// ------------------------------------------------------------------------------------------- Halo 2 accumulator
struct Halo2Decision
{
    bool entryOk = false, strideOk = false, yawOk = false, pitchOk = false, callOk = false, slotOk = false;
    bool hook = false;
    uintptr_t call = 0;   // the control update's call of the accumulator
    uintptr_t slot = 0;   // the player control globals slot
};

// The call site's rel32 must land on the entry (the instruction after it is the return address the drive writes
// after), and the record slot comes from the prologue's rip load and must lie inside the module. Both are read
// again here, from the loaded module, at the places the plan found.
inline Halo2Decision DecideHalo2(uintptr_t base, size_t size, bool opened, const drive_sites::Halo2Control& found)
{
    Halo2Decision d;
    d.entryOk = opened && found.entry != 0;
    d.strideOk = opened && found.stride != 0;
    d.yawOk = opened && found.yaw != 0;
    d.pitchOk = opened && found.pitch != 0;
    d.call = base + found.call;
    d.callOk = d.entryOk && found.call != 0 && CallReturn(base, size, found.call, found.entry) != 0;
    if (d.entryOk && found.slot != 0 &&
        static_cast<size_t>(found.entry) + drive_sites::kHalo2ControlSlotLoadOffset + 7 <= size)
    {
        // `mov rdi, [rip + disp32]`: seven bytes, the displacement in the last four.
        const uintptr_t load = base + found.entry + drive_sites::kHalo2ControlSlotLoadOffset;
        int32_t displacement = 0;
        if (Read(load + 3, &displacement, sizeof(displacement)))
        {
            d.slot = load + 7 + static_cast<intptr_t>(displacement);
            d.slotOk = d.slot >= base && d.slot + sizeof(uintptr_t) <= base + size && d.slot == base + found.slot;
        }
    }
    d.hook = d.entryOk && d.strideOk && d.yawOk && d.pitchOk && d.callOk && d.slotOk;
    return d;
}
} // namespace drive_install
