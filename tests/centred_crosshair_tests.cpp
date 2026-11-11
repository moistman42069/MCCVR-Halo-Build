// Always the centred crosshair (src/common/centred_crosshair_logic.h): each title's settings fills match the retail
// modules' own bytes, stock or patched, and their patch is the game's Centered arm; anything else at the patch site is
// refused; Halo 4's setter is the one the call reaches; the profile arrays decode to their address and stride.
#include "centred_crosshair_logic.h"

#include <cstdio>
#include <cstring>

namespace
{
using namespace centred_crosshair;

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

// The retail bytes at each match (Steam and Store alike).
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
const uint8_t kHalo4Setter[kCalleeBytes]{
    0x83, 0xFA, 0x02, 0x7D, 0x35, 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x8B,
    0xFA, 0x48, 0x8B, 0xD9, 0x33, 0xD2, 0x45, 0x84, 0xC0, 0x74, 0x0A, 0x3B, 0xB9, 0x90, 0x00, 0x00,
    0x00, 0x74, 0x02, 0xB2, 0x01, 0xE8, 0x16, 0x14, 0x00, 0x00, 0x89, 0xBB, 0x90, 0x00, 0x00, 0x00,
    0x48, 0x8B, 0x5C, 0x24, 0x30, 0x48, 0x83, 0xC4, 0x20, 0x5F, 0xC3, 0xCC, 0x33, 0xC0, 0x4C, 0x8B};

// Each site's bytes: they match, the stock instruction is recognised and its patch is what the comment says; once
// patched they still match and are recognised as patched.
void CheckSite(const centred_crosshair::Site& site, const uint8_t* stock, size_t size, const uint8_t* expected,
          size_t expectedLength, const char* what)
{
    char label[160];
    Check(PatternBytes(site.pattern) == size, (std::snprintf(label, sizeof(label), "%s: the pattern's length", what), label));
    Check(Matches(stock, site.pattern), (std::snprintf(label, sizeof(label), "%s: the stock bytes match", what), label));
    uint8_t centred[kMostPatchBytes]{};
    size_t length = 0;
    bool patched = true;
    const bool ok = CentredBytes(site.patch, stock + site.patchOffset, centred, length, patched);
    Check(ok && !patched && length == expectedLength && !std::memcmp(centred, expected, expectedLength),
          (std::snprintf(label, sizeof(label), "%s: the patch takes the Centered arm", what), label));
    uint8_t after[64]{};
    std::memcpy(after, stock, size);
    std::memcpy(after + site.patchOffset, centred, length);
    Check(Matches(after, site.pattern), (std::snprintf(label, sizeof(label), "%s: a patched fill still matches", what), label));
    Check(CentredBytes(site.patch, after + site.patchOffset, centred, length, patched) && patched,
          (std::snprintf(label, sizeof(label), "%s: and is known as patched", what), label));
}

void Sites()
{
    const uint8_t jump[]{0xEB};
    CheckSite(kHalo3.sites[0], kHalo3Fill, sizeof(kHalo3Fill), jump, 1, "Halo 3");
    CheckSite(kOdst.sites[0], kOdstFill, sizeof(kOdstFill), jump, 1, "ODST");
    CheckSite(kReach.sites[0], kReachFill, sizeof(kReachFill), jump, 1, "Reach's full fill");
    const uint8_t movEbx1[]{0xBB, 0x01, 0x00, 0x00, 0x00};
    CheckSite(kReach.sites[1], kReachChange, sizeof(kReachChange), movEbx1, 5, "Reach's changed setting: mov ebx, 1");
    CheckSite(kHalo4.sites[0], kHalo4Fill, sizeof(kHalo4Fill), jump, 1, "Halo 4's full fill");
    const uint8_t movEdx1[]{0xBA, 0x01, 0x00, 0x00, 0x00};
    CheckSite(kHalo4.sites[1], kHalo4Change, sizeof(kHalo4Change), movEdx1, 5, "Halo 4's changed setting: mov edx, 1");

    // The fields the titles name are the ones their stores write.
    Check(kHalo3.field == 0x68 && kOdst.field == 0x5C && kReach.field == 0x80 && kHalo4.field == 0x90,
          "each title's profile field");
}

void Refusals()
{
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
    Check(!CentredBytes(Patch::Jump, nullptr, centred, length, patched), "no bytes, no fill");

    uint8_t other[sizeof(kReachFill)];
    std::memcpy(other, kReachFill, sizeof(other));
    other[2] = 0x1D;   // cmp byte ptr [rsi+0x1D], 1: another setting
    Check(!Matches(other, kReach.sites[0].pattern), "a fill that reads another byte of MCC's profile does not match");
    std::memcpy(other, kReachFill, sizeof(other));
    other[13] = 0x84;   // and dword ptr [rdi+0x84], 0: another field
    Check(!Matches(other, kReach.sites[0].pattern), "a fill that stores another field does not match");
}

void Halo4Setter()
{
    const Site& site = kHalo4.sites[1];
    const uintptr_t match = 0x3FB146;
    Check(CallTarget(kHalo4Change + site.callOffset, match + site.callOffset) == 0x3C9C30,
          "Halo 4: the changed setting's call reaches the setter");
    Check(PatternBytes(site.callee) <= kCalleeBytes && Matches(kHalo4Setter, site.callee),
          "and the setter stores the profile's +0x90");
    uint8_t other[kCalleeBytes];
    std::memcpy(other, kHalo4Setter, sizeof(other));
    other[44] = 0x94;   // mov [rbx+0x94], edi
    Check(!Matches(other, site.callee), "a setter of another field is refused");
    const uint8_t notACall[]{0xE9, 0, 0, 0, 0};
    Check(!CallTarget(notACall, 0x1000), "only an E8 is a call");
}

void Arrays()
{
    // halo3.dll +0x2D84F6: movsxd r9, edx; lea rax, [rip+0x2A66870]; imul rdi, r9, 0xD78; ...
    const uint8_t halo3[]{0x4C, 0x63, 0xCA, 0x48, 0x8D, 0x05, 0x70, 0x68, 0xA6, 0x02, 0x49, 0x69, 0xF9,
                          0x78, 0x0D, 0x00, 0x00, 0x48, 0x8B, 0xD9, 0x48, 0x03, 0xF8, 0x48, 0x8B, 0xCF};
    uintptr_t array = 0;
    uint32_t stride = 0;
    Check(Matches(halo3, kHalo3.array.pattern) &&
              DecodeArray(kHalo3.array, halo3, 0x2D84F6, kHalo3.field, array, stride) && array == 0x2D3ED70 &&
              stride == 0xD78,
          "Halo 3: the four profiles' array and its stride");
    Check(FieldAddress(array, stride, 3, kHalo3.field) == 0x2D3ED70 + 3 * 0xD78 + 8 + 0x68, "user 4's field");
    // halo3odst.dll +0x303FC6.
    const uint8_t odst[]{0x4C, 0x63, 0xCA, 0x48, 0x8D, 0x05, 0x90, 0xD8, 0xA7, 0x02, 0x49, 0x69, 0xF9,
                         0xB0, 0x0E, 0x00, 0x00, 0x48, 0x8B, 0xD9, 0x48, 0x03, 0xF8, 0x48, 0x8B, 0xCF};
    Check(Matches(odst, kOdst.array.pattern) && DecodeArray(kOdst.array, odst, 0x303FC6, kOdst.field, array, stride) &&
              array == 0x2D81860 && stride == 0xEB0,
          "ODST: the four profiles' array and its stride");
    // haloreach.dll +0x2C7382: movsxd r9, edx; lea rax, [rip+0x273C8C4]; mov rbx, rcx; imul rsi, r9, 0xAB8; ...
    const uint8_t reach[]{0x4C, 0x63, 0xCA, 0x48, 0x8D, 0x05, 0xC4, 0xC8, 0x73, 0x02, 0x48, 0x8B, 0xD9,
                          0x49, 0x69, 0xF1, 0xB8, 0x0A, 0x00, 0x00, 0x48, 0x03, 0xF0, 0x48, 0x8B, 0xCE};
    Check(Matches(reach, kReach.array.pattern) &&
              DecodeArray(kReach.array, reach, 0x2C7382, kReach.field, array, stride) && array == 0x2A03C50 &&
              stride == 0xAB8,
          "Reach: the four profiles' array and its stride");
    // halo4.dll +0x3C91E3: movsxd r12, edx; lea rax, [rip+0x2D22CE3]; mov rdi, rcx; imul rsi, r12, 0xCC0; ...
    const uint8_t halo4[]{0x4C, 0x63, 0xE2, 0x48, 0x8D, 0x05, 0xE3, 0x2C, 0xD2, 0x02, 0x48, 0x8B, 0xF9,
                          0x49, 0x69, 0xF4, 0xC0, 0x0C, 0x00, 0x00, 0x48, 0x03, 0xF0, 0x48, 0x8B, 0xCE};
    Check(Matches(halo4, kHalo4.array.pattern) &&
              DecodeArray(kHalo4.array, halo4, 0x3C91E3, kHalo4.field, array, stride) && array == 0x30EBED0 &&
              stride == 0xCC0,
          "Halo 4: the four profiles' array and its stride");
    uint8_t tiny[sizeof(halo3)];
    std::memcpy(tiny, halo3, sizeof(tiny));
    tiny[13] = 0x40;
    tiny[14] = 0x00;
    Check(!DecodeArray(kHalo3.array, tiny, 0x2D84F6, kHalo3.field, array, stride),
          "a stride that cannot hold the field is refused");
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
    Sites();
    Refusals();
    Halo4Setter();
    Arrays();
    Patterns();
    std::printf("centred_crosshair_tests: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
