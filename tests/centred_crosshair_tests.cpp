// Always the centred crosshair (src/common/centred_crosshair_logic.h): the settings fill is recognised in the retail
// Halo 3 and ODST modules' own bytes, patched or not, and refused when it does not read MCC's crosshair byte; the
// profile array decodes to its address and stride.
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

// halo3.dll +0x2DA29C .. +0x2DA2D0 (the 16 bytes before the jump, then the pattern's 37).
const uint8_t kHalo3Before[16]{0x88, 0x43, 0x1A, 0x41, 0x80, 0x7E, 0x1C, 0x01,
                               0xF3, 0x0F, 0x11, 0x8D, 0x5C, 0x07, 0x00, 0x00};
const uint8_t kHalo3Jump[37]{0x74, 0x05, 0x45, 0x33, 0xDB, 0xEB, 0x06, 0x41, 0xBB, 0x01, 0x00, 0x00, 0x00,
                             0x33, 0xD2, 0x48, 0x8B, 0xCB, 0xE8, 0x4D, 0xF0, 0xFF, 0xFF, 0x80, 0xBD, 0xC0,
                             0x07, 0x00, 0x00, 0x00, 0x41, 0xB7, 0x01, 0x44, 0x89, 0x5B, 0x68};
// halo3odst.dll +0x305FED .. : the compare without a REX prefix, right before the jump.
const uint8_t kOdstBefore[16]{0x00, 0xF3, 0x0F, 0x11, 0x4B, 0x74, 0x8A, 0x46,
                              0x39, 0x88, 0x43, 0x1A, 0x80, 0x7E, 0x1C, 0x01};
const uint8_t kOdstJump[37]{0x74, 0x05, 0x45, 0x33, 0xDB, 0xEB, 0x06, 0x41, 0xBB, 0x01, 0x00, 0x00, 0x00,
                            0x33, 0xD2, 0x48, 0x8B, 0xCB, 0xE8, 0xEC, 0xEF, 0xFF, 0xFF, 0x80, 0xBD, 0x20,
                            0x08, 0x00, 0x00, 0x00, 0x41, 0xB7, 0x01, 0x44, 0x89, 0x5B, 0x5C};

void Fill()
{
    bool patched = true;
    uint8_t field = 0;
    Check(DecodeFill(kHalo3Jump, kHalo3Before, patched, field) && !patched && field == 0x68,
          "Halo 3: the fill reads MCC's byte and stores the profile's +0x68");
    Check(DecodeFill(kOdstJump, kOdstBefore, patched, field) && !patched && field == 0x5C,
          "ODST: the compare right before the jump, without REX; the profile's +0x5C");
    uint8_t jump[37];
    std::memcpy(jump, kHalo3Jump, sizeof(jump));
    jump[0] = 0xEB;
    Check(DecodeFill(jump, kHalo3Before, patched, field) && patched && field == 0x68,
          "an already patched fill is recognised as such");
    jump[0] = 0x75;
    Check(!DecodeFill(jump, kHalo3Before, patched, field), "any other jump is refused");
    uint8_t other[16];
    std::memcpy(other, kHalo3Before, sizeof(other));
    other[6] = 0x1D;   // cmp byte ptr [r14+0x1D], 1: another setting
    Check(!DecodeFill(kHalo3Jump, other, patched, field), "a fill that reads another byte of MCC's profile is refused");
    std::memcpy(other, kHalo3Before, sizeof(other));
    other[5] = 0x7C;   // [rsp + ...] needs a SIB byte: not this compare
    Check(!DecodeFill(kHalo3Jump, other, patched, field), "a compare through rsp is not the fill's");
    Check(!DecodeFill(nullptr, kHalo3Before, patched, field), "no bytes, no fill");
}

void Array()
{
    // halo3.dll +0x2D84F6: movsxd r9, edx; lea rax, [rip+0x2A66870]; imul rdi, r9, 0xD78; ...
    const uint8_t halo3[17]{0x4C, 0x63, 0xCA, 0x48, 0x8D, 0x05, 0x70, 0x68, 0xA6, 0x02,
                            0x49, 0x69, 0xF9, 0x78, 0x0D, 0x00, 0x00};
    uintptr_t array = 0;
    uint32_t stride = 0;
    Check(DecodeArray(halo3, 0x2D84F6, 0x68, array, stride) && array == 0x2D3ED70 && stride == 0xD78,
          "Halo 3: the four profiles' array and its stride");
    Check(FieldAddress(array, stride, 3, 0x68) == 0x2D3ED70 + 3 * 0xD78 + 8 + 0x68, "user 4's field");
    // halo3odst.dll +0x303FC6: lea rax, [rip+0x2A7D890]; imul rdi, r9, 0xEB0.
    const uint8_t odst[17]{0x4C, 0x63, 0xCA, 0x48, 0x8D, 0x05, 0x90, 0xD8, 0xA7, 0x02,
                           0x49, 0x69, 0xF9, 0xB0, 0x0E, 0x00, 0x00};
    Check(DecodeArray(odst, 0x303FC6, 0x5C, array, stride) && array == 0x2D81860 && stride == 0xEB0,
          "ODST: the four profiles' array and its stride");
    uint8_t tiny[17];
    std::memcpy(tiny, halo3, sizeof(tiny));
    tiny[13] = 0x40;
    tiny[14] = 0x00;
    Check(!DecodeArray(tiny, 0x2D84F6, 0x68, array, stride), "a stride that cannot hold the field is refused");
}
}

int main()
{
    Fill();
    Array();
    std::printf("centred_crosshair_tests: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
