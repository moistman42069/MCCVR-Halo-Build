// The build identities the log states (src/common/title_build_identity.h) are the ones each title's own gate
// is pinned to. If a lane is moved to a new build and this table is not, the BUILD CHECK line would call the new
// build "not the build this mod was made for" while the gate accepts it (or the reverse): this test fails first.
#include "../src/common/title_build_identity.h"
#include "../src/common/halo2_render_logic.h"
#include "../src/common/halo4_render_logic.h"
#include "../src/common/haloce_contracts.generated.h"
#include "../src/common/reach_render_logic.h"
#include <cstdio>

using namespace title_build;
namespace
{
unsigned checks = 0, failures = 0;
void Check(bool ok, const char* why)
{
    ++checks;
    if (!ok)
    {
        ++failures;
        std::printf("FAIL: %s\n", why);
    }
}
} // namespace

int main()
{
    const Identity* reach = Known(GameTitle::HaloReach);
    Check(reach && reach->peTimestamp == kReachRetailPeTimestamp && reach->sizeOfImage == kReachRetailImageSize,
          "Reach: the stated build is the one the render preflight is pinned to");
    const Identity* halo2 = Known(GameTitle::Halo2);
    Check(halo2 && halo2->peTimestamp == kHalo2RetailPeTimestamp && halo2->sizeOfImage == kHalo2RetailImageSize,
          "Halo 2: the stated build is the one the cold observation is pinned to");
    const Identity* halo4 = Known(GameTitle::Halo4);
    Check(halo4 && halo4->peTimestamp == kHalo4RetailPeTimestamp && halo4->sizeOfImage == kHalo4RetailImageSize,
          "Halo 4: the stated build is the one the cold observation is pinned to");
    const Identity* ce = Known(GameTitle::HaloCE);
    Check(ce && ce->peTimestamp == halo_ce::contract::timestamp && ce->sizeOfImage == halo_ce::contract::imageSize,
          "Halo CE: the stated build is the generated contract's");
    Check(Known(GameTitle::Halo3) && Known(GameTitle::Halo3ODST), "Halo 3 and ODST have a recorded build");
    Check(!Known(GameTitle::None) && !Known(GameTitle::Unknown), "no title: no record");

    Check(Compare(GameTitle::HaloReach, kReachRetailPeTimestamp, static_cast<uint32_t>(kReachRetailImageSize)) ==
              Match::Known,
          "the known build is known");
    Check(Compare(GameTitle::HaloReach, kReachRetailPeTimestamp + 1, static_cast<uint32_t>(kReachRetailImageSize)) ==
              Match::Different,
          "another timestamp is another build");
    Check(Compare(GameTitle::HaloReach, kReachRetailPeTimestamp,
                  static_cast<uint32_t>(kReachRetailImageSize) + 0x1000) == Match::Different,
          "another image size is another build");
    Check(Compare(GameTitle::Halo2, kReachRetailPeTimestamp, static_cast<uint32_t>(kReachRetailImageSize)) ==
              Match::Different,
          "one title's build is not another's");
    Check(Compare(GameTitle::Unknown, 1, 2) == Match::NoRecord, "a title with no record is not called different");
    Check(!CoreIsBuildGated(GameTitle::Halo3) && CoreIsBuildGated(GameTitle::HaloReach) &&
              CoreIsBuildGated(GameTitle::HaloCE) && !CoreIsBuildGated(GameTitle::Unknown),
          "Halo 3's core is the one that is not gated on a build");

    std::printf("Title build identity: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
