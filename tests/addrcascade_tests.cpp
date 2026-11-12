// addrcascade (src/common/addrcascade): the library's own consumer and unit tests.
//
// This file includes the library's header and nothing else of this project, and its target has only the library's
// own folder of this project on its include path, at /W4 /WX: that the library stays liftable is checked by building
// it this way, not asserted. It calls across the whole public surface, so a missing definition is a link error.
// Built twice: with ADDRCASCADE_FAULTS undefined (a player build's view: fault() is a constant false) and with
// ADDRCASCADE_FAULTS=1 (a dev build's: fault() follows the mask).
#include "AddressCascade.hpp"

#include <Windows.h>
#include <cstdio>
#include <cstring>

namespace
{
unsigned checks{}, failures{};
void Check(bool ok, const char* what)
{
    ++checks;
    if (!ok)
    {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}

unsigned g_logged = 0;
char g_lastLog[256]{};

void TestModuleFacts()
{
    HMODULE self = GetModuleHandleW(nullptr);

    addrcascade::ModuleRange range{};
    Check(addrcascade::module_range(self, &range), "module_range reads this executable");
    Check(range.base == reinterpret_cast<uintptr_t>(self) && range.size != 0 && range.end == range.base + range.size,
          "module_range: base, size and end agree");
    Check(!addrcascade::module_range(nullptr, &range), "module_range refuses a null module");
    Check(!addrcascade::module_range(self, nullptr), "module_range refuses a null result");

    addrcascade::ModuleIdentity id{};
    Check(addrcascade::module_identity(self, &id), "module_identity reads this executable");
    Check(id.size_of_image == range.size, "module_identity: the image size is the module's");
    Check(id.pdb_guid[0] != 0, "module_identity: the identity text is never empty");

    // A header that is not a PE image is refused, not read.
    alignas(16) static unsigned char notAnImage[4096]{};
    Check(!addrcascade::module_range(notAnImage, &range), "module_range refuses memory that is not an image");
    Check(!addrcascade::module_identity(notAnImage, &id), "module_identity refuses memory that is not an image");
    uint32_t index = 0;
    uintptr_t rva = 0;
    Check(!addrcascade::tls_index(notAnImage, &index, &rva), "tls_index refuses memory that is not an image");
    Check(!addrcascade::tls_index(self, nullptr, &rva), "tls_index refuses a null result");
    // This executable may or may not carry a TLS directory; where it does, the index lives inside the image.
    if (addrcascade::tls_index(self, &index, &rva))
        Check(rva != 0 && rva < range.size, "tls_index: the index lives inside the image");

    Check(addrcascade::readable_bytes(&range, sizeof(range)) == sizeof(range), "readable_bytes: a local is readable");
    Check(addrcascade::readable_bytes(nullptr, 16) == 0, "readable_bytes: null is not readable");
    Check(addrcascade::readable_bytes(&range, 0) == 0, "readable_bytes: a zero limit reads nothing");
    void* reserved = VirtualAlloc(nullptr, 4096, MEM_RESERVE, PAGE_NOACCESS);
    Check(reserved != nullptr && addrcascade::readable_bytes(reserved, 16) == 0,
          "readable_bytes: reserved, uncommitted memory is not readable");
    if (reserved)
        VirtualFree(reserved, 0, MEM_RELEASE);
}

// A function whose body carries a byte pattern no other code in this executable has.
__declspec(noinline) uint64_t Marked(uint64_t x)
{
    volatile uint64_t a = 0x7A3C5E1D9B2F4860ull;
    volatile uint64_t b = 0x1F6E4D2C8B7A5930ull;
    return (x ^ a) + b;
}

void TestScans()
{
    HMODULE self = GetModuleHandleW(nullptr);

    // The two 64-bit immediates above sit in this executable's code. Each is found exactly once.
    static const unsigned char kOnce[] = {0x60, 0x48, 0x2F, 0x9B, 0x1D, 0x5E, 0x3C, 0x7A};
    static const char kOnceMask[] = "xxxxxxxx";
    static_assert(sizeof(kOnce) == sizeof(kOnceMask) - 1, "signature and mask must pair up");
    Check(Marked(1) != 0, "the marked function runs");
    const addrcascade::ScanResult once = addrcascade::scan_signature(self, {kOnce, kOnceMask, sizeof(kOnce)});
    Check(once.unique(), "scan_signature: a pattern that occurs once is unique");

    // A pattern every x64 function has matches many times, and is reported as such rather than as its first hit.
    static const unsigned char kMany[] = {0x48, 0x83};
    static const char kManyMask[] = "xx";
    static_assert(sizeof(kMany) == sizeof(kManyMask) - 1, "signature and mask must pair up");
    const addrcascade::ScanResult many = addrcascade::scan_signature(self, {kMany, kManyMask, sizeof(kMany)});
    Check(many.matches > 1 && !many.unique(), "scan_signature: an idiom is ambiguous, never unique");

    // A leading wildcard must not narrow the pattern to those starting with bytes[0].
    static const unsigned char kWild[] = {0xFF, 0x48, 0x2F, 0x9B, 0x1D, 0x5E, 0x3C, 0x7A};
    static const char kWildMask[] = "?xxxxxxx";
    static_assert(sizeof(kWild) == sizeof(kWildMask) - 1, "signature and mask must pair up");
    const addrcascade::ScanResult wild = addrcascade::scan_signature(self, {kWild, kWildMask, sizeof(kWild)});
    Check(wild.unique() && wild.address == once.address, "scan_signature: a leading wildcard matches any byte");

    static const unsigned char kNone[] = {0x13, 0x37, 0xC0, 0xDE, 0x13, 0x37, 0xC0, 0xDE, 0x5A, 0xA5, 0x5A, 0xA5};
    static const char kNoneMask[] = "xxxxxxxxxxxx";
    static_assert(sizeof(kNone) == sizeof(kNoneMask) - 1, "signature and mask must pair up");
    const addrcascade::ScanResult none = addrcascade::scan_signature(self, {kNone, kNoneMask, sizeof(kNone)});
    Check(none.matches == 0 && none.address == 0 && !none.unique(), "scan_signature: a missing pattern has no match");

    Check(addrcascade::scan_signature(nullptr, {kOnce, kOnceMask, sizeof(kOnce)}).matches == 0,
          "scan_signature: a null module has no match");
    Check(addrcascade::scan_signature(self, {kOnce, kOnceMask, 0}).matches == 0,
          "scan_signature: an empty pattern has no match");

    // A string this executable holds once is found where it is.
    static const char kNeedle[] = "addrcascade-tests-needle-5b1e9c";
    Check(addrcascade::scan_string(self, kNeedle) == reinterpret_cast<uintptr_t>(&kNeedle[0]),
          "scan_string finds a string the image holds once");
    Check(addrcascade::scan_string(nullptr, "x") == 0 && addrcascade::scan_string(self, "") == 0,
          "scan_string refuses a null module and an empty string");
    // How often the compiler refers to a literal is its own business: only that the scan runs is checked.
    (void)addrcascade::scan_reference(self, reinterpret_cast<uintptr_t>(&kNeedle[0]));
    Check(addrcascade::scan_reference(nullptr, 1) == 0 && addrcascade::scan_reference(self, 0) == 0,
          "scan_reference refuses a null module and a null target");

    // The library's own proof that its two anchor scanners work, against this module.
    g_logged = 0;
    Check(addrcascade::self_test(self), "self_test finds its marker in this executable");
    Check(g_logged != 0 && std::strstr(g_lastLog, "SELFTEST") != nullptr, "self_test says what it found");
}

void TestFaults()
{
    addrcascade::set_fault_mask(0);
    Check(addrcascade::fault_mask() == 0 && !addrcascade::fault(0x1), "no fault is forced with an empty mask");
    addrcascade::set_fault_mask(0x5);
    Check(addrcascade::fault_mask() == 0x5, "the fault mask reads back");
#if defined(ADDRCASCADE_FAULTS) && ADDRCASCADE_FAULTS
    Check(addrcascade::fault(0x1) && addrcascade::fault(0x4) && !addrcascade::fault(0x2),
          "with ADDRCASCADE_FAULTS, fault() follows the mask bit by bit");
#else
    Check(!addrcascade::fault(0x1) && !addrcascade::fault(0x4),
          "without ADDRCASCADE_FAULTS, fault() is false whatever the mask holds");
#endif
    addrcascade::set_fault_mask(0);
}

void TestWatchdog()
{
    addrcascade::HookWatchdog wd{3};
    bool fired = false;
    unsigned firedCount = 0;
    for (int i = 0; i < 5; ++i)
    {
        const bool now = wd.tick(true, false);
        fired = fired || now;
        firedCount += now ? 1u : 0u;
    }
    Check(fired && wd.dead(), "the watchdog fires after its limit when the call was possible");
    Check(firedCount == 1, "the watchdog reports the flip once");
    Check(wd.counted() == 3, "the watchdog counted exactly its limit of possible ticks");

    wd.reset();
    for (int i = 0; i < 50; ++i)
        (void)wd.tick(false, false);
    Check(!wd.dead() && wd.counted() == 0, "impossible time never counts, however long it runs");

    // Possible time that is interrupted starts again from nothing.
    (void)wd.tick(true, false);
    (void)wd.tick(true, false);
    (void)wd.tick(false, false);
    Check(wd.counted() == 0, "an impossible tick discards the partial count");
    (void)wd.tick(true, false);
    (void)wd.tick(true, false);
    Check(!wd.dead(), "two possible ticks after an interruption are below the limit of three");

    // A hook that has been called is never condemned.
    addrcascade::HookWatchdog ran{2};
    for (int i = 0; i < 20; ++i)
        (void)ran.tick(true, true);
    Check(!ran.dead(), "a hook that ran is never declared dead");
}

void TestCoVariation()
{
    addrcascade::CoVariation::Config c;
    c.reference_motion_required = 10.0;
    c.wrap = 360.0;
    addrcascade::CoVariation cov{c};
    addrcascade::CoVariation::Verdict v = addrcascade::CoVariation::Verdict::Pending;
    for (int i = 0; i < 40; ++i)
        v = cov.sample(i * 1.0, i * 1.0);
    Check(v == addrcascade::CoVariation::Verdict::Match, "CoVariation: a candidate that tracks the reference matches");
    Check(cov.samples() != 0 && cov.reference_motion() >= 10.0 && cov.candidate_motion() >= 10.0,
          "CoVariation: both motions were accumulated");
    cov.reset();
    for (int i = 0; i < 40; ++i)
        v = cov.sample(0.0, i * 1.0);
    Check(v == addrcascade::CoVariation::Verdict::Mismatch, "CoVariation: a frozen candidate is a mismatch");

    // A still reference proves nothing, and ends inconclusive rather than condemning the offset.
    addrcascade::CoVariation::Config still = c;
    still.max_samples = 20;
    addrcascade::CoVariation quiet{still};
    for (int i = 0; i < 40; ++i)
        v = quiet.sample(3.0, 3.0);
    Check(v == addrcascade::CoVariation::Verdict::Inconclusive, "CoVariation: no reference motion is inconclusive");

    // A wrap step is a small motion, not most of a turn.
    addrcascade::CoVariation wrapped{c};
    (void)wrapped.sample(359.0, 359.0);
    (void)wrapped.sample(1.0, 1.0);
    Check(wrapped.reference_motion() < 3.0, "CoVariation: 359 -> 1 degrees is two degrees of motion");
}

void TestValueAgreement()
{
    addrcascade::ValueAgreement::Config vc;
    vc.tolerance = 5.0;
    vc.reference_motion_required = 10.0;
    vc.wrap = 360.0;
    {   // the correct field tracks its reference almost exactly
        addrcascade::ValueAgreement va{vc};
        addrcascade::ValueAgreement::Verdict av = addrcascade::ValueAgreement::Verdict::Pending;
        for (int i = 0; i < 40; ++i)
            av = va.sample(i * 1.0 + 0.01, i * 1.0);
        Check(av == addrcascade::ValueAgreement::Verdict::Match, "ValueAgreement: the right field matches");
        Check(va.worst_error() < 0.1 && va.worst_run() == 0 && va.samples() == 40 && va.reference_motion() >= 10.0,
              "ValueAgreement: its error, run, samples and motion are reported");
    }
    {   // the offset shifted onto the neighbouring field: it moves just as much but holds another quantity
        addrcascade::ValueAgreement va{vc};
        addrcascade::CoVariation::Config cc;
        cc.reference_motion_required = 10.0;
        cc.wrap = 360.0;
        addrcascade::CoVariation cov{cc};
        addrcascade::ValueAgreement::Verdict av = addrcascade::ValueAgreement::Verdict::Pending;
        addrcascade::CoVariation::Verdict cv = addrcascade::CoVariation::Verdict::Pending;
        for (int i = 0; i < 40; ++i)
        {
            const double reference = i * 1.0;
            const double neighbour = -17.0 - i * 0.5;
            av = va.sample(neighbour, reference);
            cv = cov.sample(neighbour, reference);
        }
        Check(av == addrcascade::ValueAgreement::Verdict::Mismatch, "ValueAgreement: the neighbouring field is a mismatch");
        Check(cv == addrcascade::CoVariation::Verdict::Match,
              "CoVariation alone passes the neighbouring field (the gap ValueAgreement closes)");
        Check(va.worst_run() >= vc.strikes_to_fail, "ValueAgreement: the failing run is reported");
    }
    {   // a static window must not credit agreement it never earned
        addrcascade::ValueAgreement va{vc};
        addrcascade::ValueAgreement::Verdict av = addrcascade::ValueAgreement::Verdict::Pending;
        for (int i = 0; i < 40; ++i)
            av = va.sample(7.0, 7.0);
        Check(av != addrcascade::ValueAgreement::Verdict::Match, "ValueAgreement: a still window is not a match");
        va.reset();
        Check(va.samples() == 0 && va.reference_motion() == 0.0, "ValueAgreement: reset starts the window again");
    }
    {   // one transient sample out of tolerance does not fail a field that otherwise agrees
        addrcascade::ValueAgreement va{vc};
        addrcascade::ValueAgreement::Verdict av = addrcascade::ValueAgreement::Verdict::Pending;
        for (int i = 0; i < 40; ++i)
            av = va.sample(i == 20 ? i * 1.0 + 60.0 : i * 1.0, i * 1.0);
        Check(av == addrcascade::ValueAgreement::Verdict::Match && va.worst_run() == 1,
              "ValueAgreement: a single stale sample is a transient, not a mismatch");
    }
}

void TestTierReporter()
{
    addrcascade::TierReporter rep;
    Check(rep.changed(0x1000, "a"), "TierReporter: the first sighting is news");
    Check(!rep.changed(0x1000, "a"), "TierReporter: the same pair again is not");
    Check(rep.changed(0x1000, "b"), "TierReporter: the same address by another tier is news");
    Check(rep.changed(0x2000, "b"), "TierReporter: another address by the same tier is news");
    Check(!rep.changed(0x2000, "b"), "TierReporter: and then it is not");
    // The tier is copied: the caller's buffer may change or go away.
    char tier[16] = "first";
    Check(rep.changed(0x3000, tier), "TierReporter: a tier from a buffer is news");
    std::memcpy(tier, "other", 6);
    Check(rep.changed(0x3000, tier), "TierReporter: the buffer's new content is another tier");
    Check(!rep.changed(0x3000, "other"), "TierReporter: compared by content, not by pointer");
    Check(rep.changed(0x3000, nullptr) && !rep.changed(0x3000, ""), "TierReporter: a null tier reads as an empty one");
}
} // namespace

int main()
{
    addrcascade::set_logger([](const char* message) {
        ++g_logged;
        std::snprintf(g_lastLog, sizeof(g_lastLog), "%s", message);
    });
    addrcascade::logf("addrcascade tests: %d", 1);
    Check(g_logged == 1 && std::strcmp(g_lastLog, "addrcascade tests: 1") == 0, "the log sink receives formatted lines");

    TestModuleFacts();
    TestScans();
    TestFaults();
    TestWatchdog();
    TestCoVariation();
    TestValueAgreement();
    TestTierReporter();

    addrcascade::set_logger(nullptr);
    addrcascade::logf("dropped");   // no sink: the message is dropped, not a fault
    std::printf("addrcascade tests: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
