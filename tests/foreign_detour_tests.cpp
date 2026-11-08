// A function another program has detoured first (src/common/foreign_detour.h): its signature with the absolute jump
// in place of its first 14 bytes, and the jump's target.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "foreign_detour.h"

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

void TestJumpedPattern()
{
    const char* draw = "48 8B C4 48 89 58 08 48 89 68 18 48 89 70 20 89 50 10 57 41 54 41 55 41 56 41 57 48 83 EC 50 "
                       "48 8B 0D ?? ?? ?? ?? 44 0F B7 40 04";
    const std::string jumped = foreign_detour::JumpedPattern(draw);
    Check(jumped == "FF 25 00 00 00 00 ?? ?? ?? ?? ?? ?? ?? ?? 20 89 50 10 57 41 54 41 55 41 56 41 57 48 83 EC 50 "
                    "48 8B 0D ?? ?? ?? ?? 44 0F B7 40 04",
          "the first 14 bytes become the jump; everything after them is kept as written");
    Check(foreign_detour::JumpedPattern("48 8B ?? 48 89 58 08 48 89 68 18 48 89 70 20 89 50 10 57 41 54 41 55 41 56 "
                                        "41 57 48 83 EC 50 48").empty(),
          "a wildcard among the first 14 bytes: what stood there is not known, so no pattern");
    Check(foreign_detour::JumpedPattern("48 8B C4 48 89 58 08 48 89 68 18 48 89 70 20 89 50").empty(),
          "too little after the jump to be unique: no pattern");
    Check(foreign_detour::JumpedPattern("  48 8B C4 48 89 58 08 48 89 68 18 48 89 70   20 89 50 10 57 41 54 41 55 41 "
                                        "56 41 57 48 83 EC 50 48  ") ==
              "FF 25 00 00 00 00 ?? ?? ?? ?? ?? ?? ?? ?? 20 89 50 10 57 41 54 41 55 41 56 41 57 48 83 EC 50 48",
          "extra spaces are only separators");
}

// Padded to a whole instruction: the jump, then NOPs where the bytes it cut were.
void TestPadding()
{
    const char* draw = "48 8B C4 48 89 58 08 48 89 68 18 48 89 70 20 89 50 10 57 41 54 41 55 41 56 41 57 48 83 EC 50 "
                       "48 8B 0D ?? ?? ?? ?? 44 0F B7 40 04";
    Check(foreign_detour::JumpedPattern(draw, 1) ==
              "FF 25 00 00 00 00 ?? ?? ?? ?? ?? ?? ?? ?? 90 89 50 10 57 41 54 41 55 41 56 41 57 48 83 EC 50 "
              "48 8B 0D ?? ?? ?? ?? 44 0F B7 40 04",
          "one NOP of padding takes the place of the byte the jump's instruction cut (Project Reclaimer's part draw)");
    Check(foreign_detour::JumpedPattern(draw, foreign_detour::kMaximumPadding + 1).empty(),
          "no more padding than an instruction's length");
    Check(foreign_detour::JumpedPattern("48 8B C4 48 89 58 08 48 89 68 18 48 89 70 ?? 89 50 10 57 41 54 41 55 41 56 "
                                        "41 57 48 83 EC 50 48 8B",
                                        1)
              .empty(),
          "a wildcard where the padding goes: what stood there is not known");
}

void TestJumpTarget()
{
    uint8_t bytes[14]{0xFF, 0x25, 0, 0, 0, 0};
    const uint64_t target = 0x00007FF75D28CE90ull;
    std::memcpy(bytes + 6, &target, sizeof(target));
    Check(foreign_detour::JumpTarget(bytes) == target, "the target is the 8 bytes after the jump's instruction");
}
} // namespace

int main()
{
    TestJumpedPattern();
    TestPadding();
    TestJumpTarget();
    std::printf("foreign_detour_tests: %u checks, %u failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
