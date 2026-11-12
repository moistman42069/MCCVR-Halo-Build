// addrcascade -- see AddressCascade.hpp for the doctrine. SPDX-License-Identifier: MIT
#include "AddressCascade.hpp"

#include <Windows.h>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace addrcascade {
namespace {

LogFn g_log = nullptr;
int   g_fault_mask = 0;

// ONE validated way into a module's headers. Everything below goes through this rather than
// repeating the DOS/NT dance -- which is the duplication this library was extracted to end, and
// which it briefly reintroduced twice inside itself.
//
// The IsBadReadPtr guards are not decoration: this library exists to survive a target that has
// changed shape, so it must not fault on the very headers it is inspecting to find out.
const IMAGE_NT_HEADERS64* nt_headers(void* module_base) {
    auto* base = static_cast<uint8_t*>(module_base);
    if (base == nullptr) return nullptr;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (IsBadReadPtr(dos, sizeof(*dos)) || dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (IsBadReadPtr(nt, sizeof(*nt)) || nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;
    return nt;
}

// The module's executable sections. Scanning the whole image also walks data and relocations --
// slower, and it invites a match inside a data blob that happens to hold the same bytes.
struct CodeRange { uint8_t* begin; size_t size; };

bool code_range(void* module_base, CodeRange* out, size_t* section_index) {
    const auto* nt = nt_headers(module_base);
    if (nt == nullptr) return false;
    auto* base = static_cast<uint8_t*>(module_base);

    const auto* sec = IMAGE_FIRST_SECTION(nt);
    for (size_t i = *section_index; i < nt->FileHeader.NumberOfSections; ++i) {
        // The section table is as suspect as the headers were -- a truncated image ends the walk
        // rather than faulting on the next entry.
        if (IsBadReadPtr(&sec[i], sizeof(sec[i]))) break;
        if ((sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) continue;
        out->begin = base + sec[i].VirtualAddress;
        out->size  = sec[i].Misc.VirtualSize;
        *section_index = i + 1;
        return true;
    }
    return false;
}

bool matches_at(const uint8_t* p, const Signature& sig) {
    for (size_t i = 0; i < sig.size; ++i) {
        if (sig.mask[i] == 'x' && p[i] != sig.bytes[i]) return false;
    }
    return true;
}

// Shortest absolute distance between two values on a wrapping scale (0 = no wrap).
double wrapped_delta(double a, double b, double wrap) {
    double d = a - b;
    if (wrap > 0.0) {
        const double half = wrap * 0.5;
        while (d >  half) d -= wrap;
        while (d < -half) d += wrap;
    }
    return std::fabs(d);
}

} // namespace

// ------------------------------------------------------------------ module facts

bool module_range(void* module_base, ModuleRange* out) {
    if (out == nullptr) return false;
    const auto* nt = nt_headers(module_base);
    if (nt == nullptr) return false;
    out->base = reinterpret_cast<uintptr_t>(module_base);
    out->size = nt->OptionalHeader.SizeOfImage;
    out->end  = out->base + out->size;
    return true;
}

bool module_identity(void* module_base, ModuleIdentity* out) {
    if (out == nullptr) return false;
    const auto* nt = nt_headers(module_base);
    if (nt == nullptr) return false;
    auto* base = static_cast<uint8_t*>(module_base);

    *out = ModuleIdentity{};
    out->size_of_image = nt->OptionalHeader.SizeOfImage;
    out->timestamp     = nt->FileHeader.TimeDateStamp;
    _snprintf_s(out->pdb_guid, sizeof(out->pdb_guid), _TRUNCATE, "unknown");

    const IMAGE_DATA_DIRECTORY& d =
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
    if (d.VirtualAddress == 0 || d.Size < sizeof(IMAGE_DEBUG_DIRECTORY)) return true;

    const auto* dbg = reinterpret_cast<const IMAGE_DEBUG_DIRECTORY*>(base + d.VirtualAddress);
    const unsigned n = d.Size / (unsigned)sizeof(IMAGE_DEBUG_DIRECTORY);
    for (unsigned i = 0; i < n && !IsBadReadPtr(&dbg[i], sizeof(dbg[i])); ++i) {
        if (dbg[i].Type != IMAGE_DEBUG_TYPE_CODEVIEW || dbg[i].AddressOfRawData == 0) continue;
        const auto* cv = base + dbg[i].AddressOfRawData;
        if (IsBadReadPtr(cv, 24)) continue;
        if (cv[0] != 'R' || cv[1] != 'S' || cv[2] != 'D' || cv[3] != 'S') continue;   // RSDS
        const GUID g = *reinterpret_cast<const GUID*>(cv + 4);
        out->pdb_age = *reinterpret_cast<const uint32_t*>(cv + 20);
        _snprintf_s(out->pdb_guid, sizeof(out->pdb_guid), _TRUNCATE,
                    "%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
                    g.Data1, g.Data2, g.Data3, g.Data4[0], g.Data4[1], g.Data4[2],
                    g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
        break;
    }
    return true;
}

bool tls_index(void* module_base, uint32_t* out_index, uintptr_t* out_rva) {
    if (out_index == nullptr) return false;
    const auto* nt = nt_headers(module_base);
    if (nt == nullptr) return false;
    auto* base = static_cast<uint8_t*>(module_base);

    const IMAGE_DATA_DIRECTORY& d = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];
    if (d.VirtualAddress == 0 || d.Size < sizeof(IMAGE_TLS_DIRECTORY64)) return false;
    const auto* tls = reinterpret_cast<const IMAGE_TLS_DIRECTORY64*>(base + d.VirtualAddress);
    if (IsBadReadPtr(tls, sizeof(*tls))) return false;

    const auto* idx = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(tls->AddressOfIndex));
    if (idx == nullptr || IsBadReadPtr(idx, sizeof(uint32_t))) return false;

    *out_index = *idx;
    if (out_rva != nullptr) {
        *out_rva = reinterpret_cast<uintptr_t>(idx) - reinterpret_cast<uintptr_t>(base);
    }
    return true;
}

size_t readable_bytes(const void* p, size_t limit) {
    if (p == nullptr || limit == 0) return 0;
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(p, &mbi, sizeof(mbi)) == 0) return 0;
    if (mbi.State != MEM_COMMIT) return 0;
    // ALLOW-LIST, not a deny-list. Denying only NOACCESS|GUARD would accept PAGE_EXECUTE, which is
    // execute-ONLY: readable on most x64 parts by accident of the paging hardware, and not something
    // to rely on. Name the protections that are genuinely readable and refuse everything else.
    constexpr DWORD READABLE = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                               PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    if ((mbi.Protect & READABLE) == 0) return 0;
    if ((mbi.Protect & PAGE_GUARD) != 0) return 0;   // a guard page reads once, then raises

    const uintptr_t region_end = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    const uintptr_t avail = region_end - reinterpret_cast<uintptr_t>(p);
    return (avail < limit) ? (size_t)avail : limit;
}

// ------------------------------------------------------------------ logging

void set_logger(LogFn fn) { g_log = fn; }

void logf(const char* fmt, ...) {
    if (g_log == nullptr) return;
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, args);
    va_end(args);
    g_log(buf);
}

// ------------------------------------------------------------------ signatures

ScanResult scan_signature(void* module_base, const Signature& sig) {
    ScanResult result{};
    if (module_base == nullptr || sig.size == 0 || sig.bytes == nullptr || sig.mask == nullptr) {
        return result;
    }

    // Cheap first-byte skip, but ONLY when the first byte is not a wildcard -- otherwise it would
    // silently narrow the pattern to those starting with sig.bytes[0], which is a wrong answer that
    // still looks like a clean unique match.
    const bool  anchored = (sig.mask[0] == 'x');
    const uint8_t first  = sig.bytes[0];

    size_t    section = 0;
    CodeRange range{};
    while (code_range(module_base, &range, &section)) {
        if (range.size < sig.size) continue;
        // A section header can describe memory that is not actually committed. Reading it would
        // fault inside a library whose whole job is to survive a target that has changed shape.
        if (IsBadReadPtr(range.begin, range.size)) continue;

        const uint8_t* const end = range.begin + (range.size - sig.size);
        for (const uint8_t* p = range.begin; p <= end; ++p) {
            if (anchored && *p != first) continue;
            if (!matches_at(p, sig)) continue;
            // Keep counting after the first hit. A second match means the pattern describes an
            // idiom rather than a function, and the caller must be told so it can refuse.
            if (result.matches == 0) result.address = reinterpret_cast<uintptr_t>(p);
            ++result.matches;
            if (result.matches > 8) return result;   // enough to prove ambiguity; stop burning time
        }
    }
    return result;
}

uintptr_t scan_string(void* module_base, const char* str) {
    if (module_base == nullptr || str == nullptr) return 0;
    const size_t len = std::strlen(str);
    if (len == 0) return 0;

    const auto* nt = nt_headers(module_base);
    if (nt == nullptr) return 0;
    auto* base = static_cast<uint8_t*>(module_base);

    // Strings live in initialised data, so this walks the SECTIONS rather than code ranges -- but
    // section by section, not as one span over SizeOfImage. The gaps between sections are not
    // guaranteed readable, and a byte-by-byte sweep across one is a fault inside a library whose
    // job is to tolerate a module that is not the shape it expected.
    const auto* sec = IMAGE_FIRST_SECTION(nt);
    uintptr_t found = 0;
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        if (IsBadReadPtr(&sec[i], sizeof(sec[i]))) break;
        const size_t size = sec[i].Misc.VirtualSize;
        if (size <= len) continue;
        uint8_t* const begin = base + sec[i].VirtualAddress;
        if (IsBadReadPtr(begin, size)) continue;

        for (size_t j = 0; j + len + 1 <= size; ++j) {
            if (begin[j] != (uint8_t)str[0]) continue;
            if (std::memcmp(begin + j, str, len) != 0) continue;
            if (begin[j + len] != 0) continue;          // require NUL termination
            if (found != 0) return 0;                   // ambiguous -- refuse, same rule as above
            found = reinterpret_cast<uintptr_t>(begin + j);
        }
    }
    return found;
}

uintptr_t scan_reference(void* module_base, uintptr_t target) {
    if (module_base == nullptr || target == 0) return 0;

    size_t    section = 0;
    CodeRange range{};
    uintptr_t found = 0;
    while (code_range(module_base, &range, &section)) {
        if (range.size < 8) continue;
        if (IsBadReadPtr(range.begin, range.size)) continue;   // as scan_signature does
        // x86-64 RIP-relative operands are a 32-bit signed displacement from the END of the
        // instruction. Without decoding we cannot know the instruction length, so test the common
        // case: a 4-byte displacement whose next-instruction address lands exactly on the target.
        // Checked at every offset, which over-approximates -- hence the ambiguity refusal below.
        for (size_t i = 0; i + 8 <= range.size; ++i) {
            const int32_t disp = *reinterpret_cast<const int32_t*>(range.begin + i);
            const uintptr_t next = reinterpret_cast<uintptr_t>(range.begin + i + 4);
            if (next + static_cast<intptr_t>(disp) != target) continue;
            if (found != 0) return 0;                   // more than one -- caller must not guess
            found = reinterpret_cast<uintptr_t>(range.begin + i);
        }
    }
    return found;
}

// ------------------------------------------------------------------ self-test
//
// Deliberately odd so it cannot collide with anything else in the host binary, and volatile so the
// optimiser cannot fold the reference away and leave nothing for scan_reference to find.
static const volatile char SELFTEST_MARKER[] = "addrcascade//marker//7f3a1c<do-not-localise>";

bool self_test(void* own_module_base) {
    char buf[64];
    size_t i = 0;
    for (; i + 1 < sizeof(buf) && SELFTEST_MARKER[i] != '\0'; ++i) buf[i] = SELFTEST_MARKER[i];
    buf[i] = '\0';

    const uintptr_t str_at = scan_string(own_module_base, buf);
    if (str_at == 0) {
        logf("[addrcascade] SELFTEST: scan_string did NOT find its own marker -- string scanning is "
             "not working against this module, so do not trust scan_reference either.");
        return false;
    }

    const uintptr_t ref_at = scan_reference(own_module_base, str_at);
    logf("[addrcascade] SELFTEST: scan_string found the marker at 0x%llX; scan_reference %s. "
         "Both anchor scanners executed.",
         (unsigned long long)str_at,
         ref_at != 0 ? "located a unique RIP-relative reference to it"
                     : "found no UNIQUE reference (0 or several) -- expected for some codegen, and "
                       "refusing an ambiguous answer is the intended behaviour");
    return true;
}

// ------------------------------------------------------------------ fault injection

void set_fault_mask(int mask) { g_fault_mask = mask; }
int  fault_mask()             { return g_fault_mask; }

// ------------------------------------------------------------------ watchdog

bool HookWatchdog::tick(bool event_possible, bool event_happened) {
    if (m_dead || event_happened) return false;

    // THE WHOLE POINT: time in which the call was impossible is not evidence of anything. Reset
    // rather than merely pause, or a transition leaves a stale partial count behind that gets
    // topped up later and fires against a healthy target.
    if (!event_possible) { m_counted = 0; return false; }

    if (++m_counted < m_limit) return false;
    m_dead = true;
    return true;
}

// ------------------------------------------------------------------ co-variation

void CoVariation::reset() {
    m_have_last   = false;
    m_cand_motion = 0.0;
    m_ref_motion  = 0.0;
    m_samples     = 0;
}

CoVariation::Verdict CoVariation::sample(double candidate, double reference) {
    if (!m_have_last) {
        m_have_last = true;
        m_last_cand = candidate;
        m_last_ref  = reference;
        return Verdict::Pending;
    }

    m_cand_motion += wrapped_delta(candidate, m_last_cand, m_cfg.wrap);
    m_ref_motion  += wrapped_delta(reference, m_last_ref,  m_cfg.wrap);
    m_last_cand = candidate;
    m_last_ref  = reference;
    ++m_samples;

    // Only a window that actually SAW reference movement is evidence. A quiet window means the
    // player stood still, which says nothing about the offset -- do not let it condemn anything.
    if (m_ref_motion < m_cfg.reference_motion_required) {
        return (m_samples >= m_cfg.max_samples) ? Verdict::Inconclusive : Verdict::Pending;
    }

    return (m_cand_motion >= m_ref_motion * m_cfg.candidate_motion_floor)
        ? Verdict::Match
        : Verdict::Mismatch;
}

// ------------------------------------------------------------------ value agreement

void ValueAgreement::reset() {
    m_have_last  = false;
    m_ref_motion = 0.0;
    m_worst      = 0.0;
    m_samples    = 0;
    m_strikes    = 0;
}

ValueAgreement::Verdict ValueAgreement::sample(double candidate, double reference) {
    const double err = wrapped_delta(candidate, reference, m_cfg.wrap);
    if (err > m_worst) m_worst = err;

    if (m_have_last) m_ref_motion += wrapped_delta(reference, m_last_ref, m_cfg.wrap);
    m_last_ref  = reference;
    m_have_last = true;
    ++m_samples;

    // Disagreement is decided on a RUN, not a single sample: the two values are read a moment apart,
    // so one of them can legitimately be a tick stale mid-swing. A real wrong-field reads wrong on
    // every sample, so a short run separates them without needing a wider tolerance.
    if (err > m_cfg.tolerance) {
        ++m_strikes;
        if (m_strikes > m_worst_run) m_worst_run = m_strikes;
        if (m_strikes >= m_cfg.strikes_to_fail) return Verdict::Mismatch;
    } else {
        m_strikes = 0;
    }

    if (m_samples < m_cfg.min_samples) return Verdict::Pending;
    if (m_ref_motion < m_cfg.reference_motion_required) {
        return (m_samples >= m_cfg.max_samples) ? Verdict::Inconclusive : Verdict::Pending;
    }
    // Enough samples, enough movement, and the last run of samples agreed.
    return (m_strikes == 0) ? Verdict::Match : Verdict::Pending;
}

// ------------------------------------------------------------------ tier reporting

bool TierReporter::changed(uintptr_t address, const char* tier) {
    const char* const t = (tier != nullptr) ? tier : "";
    const bool same_tier = m_have && std::strncmp(m_last_tier, t, TIER_MAX - 1) == 0;
    if (m_have && address == m_last_address && same_tier) return false;

    m_last_address = address;
    // Copy rather than borrow -- see the header. Truncation is harmless: two tier names that agree
    // for 31 characters are the same rung for reporting purposes.
    _snprintf_s(m_last_tier, TIER_MAX, _TRUNCATE, "%s", t);
    m_have = true;
    return true;
}

} // namespace addrcascade
