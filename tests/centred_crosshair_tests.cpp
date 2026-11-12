// Always the centred crosshair (src/common/centred_crosshair_logic.h, centred_crosshair_sites.h): each title's settings
// fills (CE and Halo 2: its centred check) match the retail modules' own bytes, stock or patched, by both routes, and
// their patch is the game's Centered arm; anything else at the patch site is refused; Halo 4's setter is the one the
// call reaches; the profiles' callers give the recorded array and stride. Then a title resolved on a synthetic module
// by the address cascade's resolver: by both routes, by either alone, by neither, and with the instruction changed.
#include "centred_crosshair_sites.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
using namespace centred_crosshair;
using centred_crosshair_sites::Fill;
using centred_crosshair_sites::TitleSites;

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

// The retail bytes at each whole signature's match (Steam and Store alike).
// halo3.dll +0x2DA29F.
const uint8_t kHalo3Fill[]{0x41, 0x80, 0x7E, 0x1C, 0x01, 0xF3, 0x0F, 0x11, 0x8D, 0x5C, 0x07, 0x00, 0x00,
                           0x74, 0x05, 0x45, 0x33, 0xDB, 0xEB, 0x06, 0x41, 0xBB, 0x01, 0x00, 0x00, 0x00,
                           0x33, 0xD2, 0x48, 0x8B, 0xCB, 0xE8, 0x4D, 0xF0, 0xFF, 0xFF, 0x80, 0xBD, 0xC0,
                           0x07, 0x00, 0x00, 0x00, 0x41, 0xB7, 0x01, 0x44, 0x89, 0x5B, 0x68};
// halo3odst.dll +0x305FF9.
const uint8_t kOdstFill[]{0x80, 0x7E, 0x1C, 0x01, 0x74, 0x05, 0x45, 0x33, 0xDB, 0xEB, 0x06, 0x41, 0xBB, 0x01,
                          0x00, 0x00, 0x00, 0x33, 0xD2, 0x48, 0x8B, 0xCB, 0xE8, 0xEC, 0xEF, 0xFF, 0xFF, 0x80,
                          0xBD, 0x20, 0x08, 0x00, 0x00, 0x00, 0x41, 0xB7, 0x01, 0x44, 0x89, 0x5B, 0x5C};
// haloreach.dll +0x2C9664 and +0x2EE03F.
const uint8_t kReachFill[]{0x80, 0x7E, 0x1C, 0x01, 0x74, 0x0E, 0xE8, 0xF5, 0xF0, 0xFF, 0xFF, 0x83,
                           0xA7, 0x80, 0x00, 0x00, 0x00, 0x00, 0xEB, 0x0F, 0xE8, 0xE7, 0xF0, 0xFF,
                           0xFF, 0xC7, 0x87, 0x80, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00};
const uint8_t kReachChange[]{0x44, 0x89, 0x67, 0x48, 0x41, 0x0F, 0xB6, 0x5E, 0x1C, 0x83, 0xFB, 0x02, 0x73,
                             0x17, 0x3B, 0x9F, 0x80, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xCF, 0x0F, 0x95, 0xC2,
                             0xE8, 0x06, 0xA7, 0xFD, 0xFF, 0x89, 0x9F, 0x80, 0x00, 0x00, 0x00};
// halo4.dll +0x3CC3EF, +0x3FB146 and the setter it calls, +0x3C9C30.
const uint8_t kHalo4Fill[]{0x80, 0x7E, 0x1C, 0x01, 0x74, 0x04, 0x33, 0xDB, 0xEB, 0x05, 0xBB, 0x01, 0x00, 0x00,
                           0x00, 0x48, 0x8B, 0x7C, 0x24, 0x30, 0x33, 0xD2, 0x48, 0x8B, 0xCF, 0xE8, 0x63, 0xEC,
                           0xFF, 0xFF, 0x80, 0x7C, 0x24, 0x21, 0x00, 0x89, 0x9F, 0x90, 0x00, 0x00, 0x00};
const uint8_t kHalo4Change[]{0x45, 0x8A, 0xC5, 0x41, 0x0F, 0xB6, 0x56, 0x1C, 0x48, 0x8B, 0xCF,
                             0xE8, 0xDA, 0xEA, 0xFC, 0xFF, 0xE8, 0x7D, 0x14, 0xCA, 0xFF, 0x45,
                             0x8A, 0xC5, 0x48, 0x8B, 0xCF, 0x84, 0xC0, 0x0F, 0x84};
const uint8_t kHalo4Setter[]{
    0x83, 0xFA, 0x02, 0x7D, 0x35, 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x8B,
    0xFA, 0x48, 0x8B, 0xD9, 0x33, 0xD2, 0x45, 0x84, 0xC0, 0x74, 0x0A, 0x3B, 0xB9, 0x90, 0x00, 0x00,
    0x00, 0x74, 0x02, 0xB2, 0x01, 0xE8, 0x16, 0x14, 0x00, 0x00, 0x89, 0xBB, 0x90, 0x00, 0x00, 0x00};
// halo1.dll +0xBF8B7 and halo2.dll +0x3C1E2: the centred checks.
const uint8_t kHaloCECheck[]{0x0F, 0xBF, 0xC9, 0xE8, 0xA9, 0x70, 0xA2, 0x00, 0x48, 0x63, 0xD0, 0x48, 0x8D, 0x0D, 0x53,
                             0x59, 0xA4, 0x02, 0x48, 0x69, 0xC2, 0xCC, 0x0A, 0x00, 0x00, 0x80, 0x3C, 0x08, 0x01, 0x0F,
                             0x84, 0xAA, 0x00, 0x00, 0x00, 0x48, 0x0F, 0xBF, 0xC3, 0x48, 0x6B, 0xC8, 0x58};
const uint8_t kHalo2Check[]{0x83, 0xF9, 0x03, 0x77, 0x32, 0x48, 0x63, 0xC1, 0x48, 0x69, 0xD0, 0xCC, 0x0A, 0x00, 0x00, 0x48,
                            0x8D, 0x05, 0x94, 0x53, 0xA4, 0x01, 0x80, 0x3C, 0x02, 0x01, 0x75, 0x1B, 0xE8, 0x2D, 0x8D, 0x68,
                            0x00, 0x84, 0xC0, 0x0F, 0xB6, 0xCB, 0xBA, 0x01, 0x00, 0x00, 0x00, 0x0F, 0x44, 0xCA};

// Each fill's bytes: the whole signature and the core both match, stock and patched; the stock instruction is
// recognised and its patch is what the comment says; once patched it is recognised as patched.
void CheckFill(const Fill& fill, const uint8_t* stock, size_t size, const uint8_t* expected, size_t expectedLength,
               const char* what)
{
    char label[160];
    const auto say = [&label, what](const char* text) {
        std::snprintf(label, sizeof(label), "%s: %s", what, text);
        return label;
    };
    Check(PatternBytes(fill.whole.pattern) == size, say("the whole signature's length"));
    Check(Matches(stock, fill.whole.pattern), say("the stock bytes match the whole signature"));
    const size_t coreAt = fill.wholePatch - fill.corePatch;   // the core, inside the whole signature's bytes
    Check(fill.wholePatch >= fill.corePatch && coreAt + PatternBytes(fill.core.pattern) <= size &&
              Matches(stock + coreAt, fill.core.pattern),
          say("and its core"));
    uint8_t centred[kMostPatchBytes]{};
    size_t length = 0;
    bool patched = true;
    const bool ok = CentredBytes(fill.patch, stock + fill.wholePatch, centred, length, patched);
    Check(ok && !patched && length == expectedLength && !std::memcmp(centred, expected, expectedLength),
          say("the patch takes the Centered arm"));
    uint8_t after[64]{};
    std::memcpy(after, stock, size);
    std::memcpy(after + fill.wholePatch, centred, length);
    Check(Matches(after, fill.whole.pattern) && Matches(after + coreAt, fill.core.pattern),
          say("a patched fill still matches both"));
    Check(CentredBytes(fill.patch, after + fill.wholePatch, centred, length, patched) && patched,
          say("and is known as patched"));
}

void Fills()
{
    using namespace centred_crosshair_sites;
    const uint8_t jump[]{0xEB};
    CheckFill(kHalo3.fills[0], kHalo3Fill, sizeof(kHalo3Fill), jump, 1, "Halo 3");
    CheckFill(kOdst.fills[0], kOdstFill, sizeof(kOdstFill), jump, 1, "ODST");
    CheckFill(kReach.fills[0], kReachFill, sizeof(kReachFill), jump, 1, "Reach's full fill");
    const uint8_t movEbx1[]{0xBB, 0x01, 0x00, 0x00, 0x00};
    CheckFill(kReach.fills[1], kReachChange, sizeof(kReachChange), movEbx1, 5, "Reach's changed setting: mov ebx, 1");
    CheckFill(kHalo4.fills[0], kHalo4Fill, sizeof(kHalo4Fill), jump, 1, "Halo 4's full fill");
    const uint8_t movEdx1[]{0xBA, 0x01, 0x00, 0x00, 0x00};
    CheckFill(kHalo4.fills[1], kHalo4Change, sizeof(kHalo4Change), movEdx1, 5, "Halo 4's changed setting: mov edx, 1");
    const uint8_t nopJmp[]{0x90, 0xE9};
    CheckFill(kHaloCE.fills[0], kHaloCECheck, sizeof(kHaloCECheck), nopJmp, 2, "Halo CE's check: always the centred answer");
    const uint8_t nops[]{0x90, 0x90};
    CheckFill(kHalo2.fills[0], kHalo2Check, sizeof(kHalo2Check), nops, 2, "Halo 2's check: on to the centred answer");

    Check(!kHaloCE.profiles.caller.pattern && !kHalo2.profiles.caller.pattern,
          "CE and Halo 2 have no profile copy of their own to set");
    Check(kHalo3.field == 0x68 && kOdst.field == 0x5C && kReach.field == 0x80 && kHalo4.field == 0x90,
          "each title's profile field");
    for (const TitleSites* t : kTitles)
        Check(For(t->title) == t, "each title's table is found by its title");
    Check(!For(GameTitle::None), "no table for no title");
}

void Refusals()
{
    using namespace centred_crosshair_sites;
    uint8_t centred[kMostPatchBytes]{};
    size_t length = 0;
    bool patched = false;
    const uint8_t jne[]{0x75, 0x05};
    Check(!CentredBytes(Patch::Jump, jne, centred, length, patched), "any other jump is refused");
    const uint8_t hook[]{0xE9, 0x00, 0x00, 0x00, 0x00};
    Check(!CentredBytes(Patch::Jump, hook, centred, length, patched), "a hook at the jump is refused");
    Check(!CentredBytes(Patch::Load, hook, centred, length, patched), "a hook at the load is refused");
    const uint8_t otherByte[]{0x41, 0x0F, 0xB6, 0x5E, 0x1D};
    Check(!CentredBytes(Patch::Load, otherByte, centred, length, patched), "a load of another of MCC's bytes is refused");
    const uint8_t throughSib[]{0x41, 0x0F, 0xB6, 0x5C, 0x1C};
    Check(!CentredBytes(Patch::Load, throughSib, centred, length, patched), "a load with a SIB byte is not this one");
    const uint8_t intoHighRegister[]{0x45, 0x0F, 0xB6, 0x5E, 0x1C};
    Check(!CentredBytes(Patch::Load, intoHighRegister, centred, length, patched),
          "a load into r8-r15 (no five-byte mov) is refused");
    const uint8_t movEbx2[]{0xBB, 0x02, 0x00, 0x00, 0x00};
    Check(!CentredBytes(Patch::Load, movEbx2, centred, length, patched), "a mov of any other value is not the patch");
    const uint8_t jneNear[]{0x0F, 0x85, 0xAA, 0x00, 0x00, 0x00};
    Check(!CentredBytes(Patch::NearJump, jneNear, centred, length, patched), "a near jne is not CE's check");
    const uint8_t je[]{0x74, 0x1B};
    Check(!CentredBytes(Patch::Fallthrough, je, centred, length, patched), "a je is not Halo 2's check");
    Check(!CentredBytes(Patch::Jump, nullptr, centred, length, patched), "no bytes, no fill");

    uint8_t other[sizeof(kReachFill)];
    std::memcpy(other, kReachFill, sizeof(other));
    other[2] = 0x1D;   // cmp byte ptr [rsi+0x1D], 1: another setting
    Check(!Matches(other, kReach.fills[0].whole.pattern) && !Matches(other, kReach.fills[0].core.pattern),
          "a fill that reads another byte of MCC's profile matches neither route");
    std::memcpy(other, kReachFill, sizeof(other));
    other[13] = 0x84;   // and dword ptr [rdi+0x84], 0: another field
    Check(!Matches(other, kReach.fills[0].whole.pattern) && !Matches(other, kReach.fills[0].core.pattern),
          "a fill that stores another field matches neither route");
}

void Halo4Setter()
{
    using namespace centred_crosshair_sites;
    const Fill& fill = kHalo4.fills[1];
    Check(Matches(kHalo4Setter, kHalo4.setter.pattern) && PatternBytes(kHalo4.setter.pattern) == sizeof(kHalo4Setter),
          "Halo 4: the setter stores the profile's +0x90");
    // The call after the instruction: E8 rel32 at the instruction + setterCall.
    const uint32_t call = fill.whole.rva + fill.wholePatch + fill.setterCall;
    const uint8_t* e8 = kHalo4Change + fill.wholePatch + fill.setterCall;
    int32_t rel = 0;
    std::memcpy(&rel, e8 + 1, sizeof(rel));
    Check(e8[0] == 0xE8 && call + 5 + rel == kHalo4.setter.rva, "and the changed setting's call reaches it");
    uint8_t other[sizeof(kHalo4Setter)];
    std::memcpy(other, kHalo4Setter, sizeof(other));
    other[44] = 0x94;   // mov [rbx+0x94], edi
    Check(!Matches(other, kHalo4.setter.pattern), "a setter of another field is refused");
}

void Arrays()
{
    using namespace centred_crosshair_sites;
    struct Caller
    {
        const TitleSites* title;
        uint8_t bytes[26];
    };
    // movsxd; lea rax, [rip + array]; (mov rbx/rdi, rcx;) imul reg, reg, stride; ...
    const Caller callers[]{
        {&kHalo3, {0x4C, 0x63, 0xCA, 0x48, 0x8D, 0x05, 0x70, 0x68, 0xA6, 0x02, 0x49, 0x69, 0xF9,
                   0x78, 0x0D, 0x00, 0x00, 0x48, 0x8B, 0xD9, 0x48, 0x03, 0xF8, 0x48, 0x8B, 0xCF}},
        {&kOdst, {0x4C, 0x63, 0xCA, 0x48, 0x8D, 0x05, 0x90, 0xD8, 0xA7, 0x02, 0x49, 0x69, 0xF9,
                  0xB0, 0x0E, 0x00, 0x00, 0x48, 0x8B, 0xD9, 0x48, 0x03, 0xF8, 0x48, 0x8B, 0xCF}},
        {&kReach, {0x4C, 0x63, 0xCA, 0x48, 0x8D, 0x05, 0xC4, 0xC8, 0x73, 0x02, 0x48, 0x8B, 0xD9,
                   0x49, 0x69, 0xF1, 0xB8, 0x0A, 0x00, 0x00, 0x48, 0x03, 0xF0, 0x48, 0x8B, 0xCE}},
        {&kHalo4, {0x4C, 0x63, 0xE2, 0x48, 0x8D, 0x05, 0xE3, 0x2C, 0xD2, 0x02, 0x48, 0x8B, 0xF9,
                   0x49, 0x69, 0xF4, 0xC0, 0x0C, 0x00, 0x00, 0x48, 0x03, 0xF0, 0x48, 0x8B, 0xCE}},
    };
    for (const Caller& c : callers)
    {
        const Profiles& p = c.title->profiles;
        int32_t displacement = 0;
        uint32_t stride = 0;
        std::memcpy(&displacement, c.bytes + kArrayLeaOffset + kArrayLeaDisplacement, sizeof(displacement));
        std::memcpy(&stride, c.bytes + p.strideOffset, sizeof(stride));
        char label[96];
        std::snprintf(label, sizeof(label), "%s: the profiles' caller gives the recorded array and stride",
                      c.title->name);
        Check(Matches(c.bytes, p.caller.pattern) &&
                  p.caller.rva + kArrayLeaOffset + kArrayLeaLength + displacement == p.arrayRva && stride == p.stride &&
                  stride >= kProfileInElement + c.title->field + sizeof(int32_t),
              label);
    }
    Check(FieldAddress(0x2D3ED70, 0xD78, 3, 0x68) == 0x2D3ED70 + 3 * 0xD78 + 8 + 0x68, "user 4's field");
}

// A synthetic module holding Halo 2's check function as the retail module has it: its entry, a jump, then the
// whole signature (the core inside it), at code RVA kAt. Filler 0xCC.
struct Module
{
    static constexpr uint32_t kCode = 0x1000, kAt = 0x1200, kSize = 0x1000;
    std::vector<uint8_t> bytes;
    drive_resolve::RuntimeFunction function{};
    drive_resolve::Image view{};

    Module()
    {
        bytes.assign(kCode + kSize + 0x100, 0xCC);
        const uint8_t entry[]{0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x32, 0xDB, 0x48, 0x83,
                              0x3D, 0x11, 0x22, 0x33, 0x44, 0x00, 0x74, 0x32};
        std::memcpy(bytes.data() + kAt, entry, sizeof(entry));
        std::memcpy(bytes.data() + kAt + sizeof(entry), kHalo2Check, sizeof(kHalo2Check));
        const uint32_t unwind = kCode + kSize;
        const uint8_t info[4]{0x01, 0x04, 0x00, 0x00};
        std::memcpy(bytes.data() + unwind, info, sizeof(info));
        function = {kAt, kAt + 0x52, unwind};
    }
    uint32_t Whole() const { return kAt + 0x12; }
    uint32_t Instruction() const { return Whole() + centred_crosshair_sites::kHalo2.fills[0].wholePatch; }
    const drive_resolve::Image& Finish()
    {
        view = drive_resolve::Image{};
        view.base = bytes.data();
        view.size = static_cast<uint32_t>(bytes.size());
        view.executable[0] = drive_resolve::Range{kCode, kSize};
        view.executableCount = 1;
        view.functions = &function;
        view.functionCount = 1;
        return view;
    }
};

centred_crosshair_sites::Resolved Run(Module& m, drive_resolve::Report* report = nullptr)
{
    drive_resolve::Resolver r(m.Finish(), 0);
    const centred_crosshair_sites::Resolved out = centred_crosshair_sites::Resolve(r, centred_crosshair_sites::kHalo2);
    if (report)
        *report = r.report();
    return out;
}

void Resolution()
{
    using drive_resolve::Verdict;
    {
        Module m;
        drive_resolve::Report report;
        const auto out = Run(m, &report);
        Check(out.fills && out.at[0] == m.Instruction() && report.Complete(),
              "both routes find the check, and they agree");
        Check(report.count == 4 && report.lines[0].verdict == Verdict::Moved && report.lines[2].verdict == Verdict::Moved,
              "where it is not recorded, it is reported moved");
    }
    {
        Module m;
        m.bytes[m.Whole() + sizeof(kHalo2Check) - 1] = 0xCB;   // the whole signature's last byte, outside the core
        drive_resolve::Report report;
        const auto out = Run(m, &report);
        Check(out.fills && out.at[0] == m.Instruction() && report.lines[0].verdict == Verdict::Missing &&
                  !report.Complete(),
              "the whole signature lost: the core in its function still finds the check, and the loss is reported");
    }
    {
        Module m;
        m.bytes[Module::kAt + 1] = 0x55;   // another entry
        drive_resolve::Report report;
        const auto out = Run(m, &report);
        Check(out.fills && out.at[0] == m.Instruction() && report.lines[1].verdict == Verdict::Missing,
              "the function lost: the whole signature still finds the check");
    }
    {
        Module m;
        m.bytes[Module::kAt + 1] = 0x55;
        m.bytes[m.Whole() + 0x16] = 0x81;   // cmp byte ptr [rdx+rax] is gone from both routes
        Check(!Run(m).fills, "lost by both routes: not patched");
    }
    {
        Module m;
        m.bytes[m.Instruction()] = 0x74;   // je: both routes match (the instruction is wildcarded), the check is not it
        Check(!Run(m).fills, "an instruction that is not the check's is refused");
    }
    {
        Module m;
        m.bytes[m.Instruction()] = 0x90;
        m.bytes[m.Instruction() + 1] = 0x90;
        const auto out = Run(m);
        Check(out.fills && out.at[0] == m.Instruction(), "a check the mod already patched is found and recognised");
    }
}

void Patterns()
{
    Check(PatternBytes("") == 0 && PatternBytes("90") == 1 && PatternBytes("90 ?? 90") == 3, "pattern lengths");
    const uint8_t bytes[]{0x90, 0x12, 0xAB};
    Check(Matches(bytes, "90 ?? AB") && Matches(bytes, "90 ?? ab"), "hex and wildcards, either case");
    Check(!Matches(bytes, "90 ?? AC"), "a different byte");
    Check(!Matches(bytes, "90 1"), "a half byte is not a pattern");
}
}

int main()
{
    Fills();
    Refusals();
    Halo4Setter();
    Arrays();
    Resolution();
    Patterns();
    std::printf("centred_crosshair_tests: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
