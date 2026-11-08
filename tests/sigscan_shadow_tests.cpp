// THE SCAN SHADOW (sigscan.h, SetScanShadow): searches read a copy of part of an image instead of the image itself.
// Project Reclaimer 0.10 has put a call to its trap over the first five bytes of every function in halo3.dll's
// image; the copy has the DLL's own bytes there (src/dll/lifted_engine.cpp). This runs the production scanner on a
// synthetic image in this process.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include "../src/dll/sigscan.h"

static int failures = 0;
static void Check(bool condition, const char* description)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", description);
        ++failures;
    }
}

// A 0x400-byte image: a header, then a "code section" at +0x100 of 0x200 bytes holding two functions, then data.
static std::vector<uint8_t> g_image(0x400, 0xCC);
static std::vector<uint8_t> g_copy;
static bool g_shadowOn = false;
static constexpr size_t kCode = 0x100, kCodeSize = 0x200;

static bool Shadow(uintptr_t& start, size_t& size, const uint8_t*& copy)
{
    if (!g_shadowOn)
        return false;
    start = reinterpret_cast<uintptr_t>(g_image.data()) + kCode;
    size = g_copy.size();
    copy = g_copy.data();
    return true;
}

int main()
{
    // Function 1 at +0x100: mov [rsp+8], rbx; push rdi; sub rsp, 0x30; mov rax, [rip+0x11223344]
    const uint8_t f1[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x30,
                          0x48, 0x8B, 0x05, 0x44, 0x33, 0x22, 0x11};
    // Function 2 at +0x180: mov rax, [rip+0x100]; ret (its rip-relative load starts in its first five bytes)
    const uint8_t f2[] = {0x48, 0x8B, 0x05, 0x00, 0x01, 0x00, 0x00, 0xC3};
    std::memcpy(g_image.data() + 0x100, f1, sizeof(f1));
    std::memcpy(g_image.data() + 0x180, f2, sizeof(f2));
    // A pattern in the data after the code section, and one in the header before it.
    const uint8_t data[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01};
    std::memcpy(g_image.data() + 0x350, data, sizeof(data));
    std::memcpy(g_image.data() + 0x20, data, sizeof(data));
    g_copy.assign(g_image.begin() + kCode, g_image.begin() + kCode + kCodeSize);
    // The client's traps: E8 rel32 over each function's first five bytes.
    for (const size_t at : {size_t{0x100}, size_t{0x180}})
    {
        g_image[at] = 0xE8;
        const int32_t rel = 0x1000;
        std::memcpy(g_image.data() + at + 1, &rel, 4);
    }
    const auto base = reinterpret_cast<uintptr_t>(g_image.data());
    const size_t size = g_image.size();
    const char* head1 = "48 89 5C 24 08 57 48 83 EC 30 48 8B 05 ?? ?? ?? ??";
    const char* head2 = "48 8B 05 ?? ?? ?? ?? C3";

    // No shadow: the traps hide both heads.
    sig::SetScanShadow(&Shadow);
    Check(sig::Find(base, size, head1) == 0, "without the copy, a trapped head is not found");
    Check(sig::Find(base, size, head2) == 0, "without the copy, the second trapped head is not found");

    g_shadowOn = true;
    Check(sig::Find(base, size, head1) == base + 0x100, "the copy's head is found at the image's address");
    Check(sig::Find(base, size, head2) == base + 0x180, "the second head is found at the image's address");
    Check(sig::Find(base + 0x101, size - 0x101, head1) == 0, "a search that starts past the head finds no second one");
    Check(sig::Find(base + 0x101, size - 0x101, head2) == base + 0x180,
          "a search starting inside the copy reads it from there on");
    Check(sig::Find(base, size, "DE AD BE EF 01") == base + 0x20, "the header before the copy is the image's");
    Check(sig::Find(base + 0x30, size - 0x30, "DE AD BE EF 01") == base + 0x350,
          "the data after the copy is the image's");
    Check(sig::Find(base, size, "E8 00 10 00 00") == 0, "the traps themselves are not seen through the copy");
    Check(sig::Find(base + 0x120, 0x40, head2) == 0, "a range that ends before the head does not find it");

    // A rip-relative displacement read in the head reads the copy; CopyUnhooked too.
    Check(sig::RipTarget(base + 0x183, base + 0x187) == base + 0x187 + 0x100,
          "RipTarget reads the displacement through the copy");
    uint8_t out[8]{};
    sig::CopyUnhooked(base + 0x17E, out, sizeof(out));
    Check(out[0] == 0xCC && out[1] == 0xCC && out[2] == 0x48 && out[3] == 0x8B && out[4] == 0x05,
          "CopyUnhooked reads the copy where it has one and the image elsewhere");

    g_shadowOn = false;
    Check(sig::Find(base, size, head1) == 0, "the copy withdrawn, the image is read again");

    if (failures)
        return 1;
    std::printf("sigscan shadow: all checks passed\n");
    return 0;
}
