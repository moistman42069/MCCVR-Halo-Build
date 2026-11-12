#include "../src/common/reach_render_logic.h"
#include "../src/common/legacy_runtime_weapon_bounds.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static unsigned checks = 0;
static void Check(bool ok, const char* message)
{
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

enum class GameTitle { Halo3, Halo3ODST, HaloReach };
struct BoneMatrix { float unused{}; };
struct ReachContext {
    const BoneMatrix* source{}; uint32_t generation{};
    int liveSourceCount{}; ReachFpBodyLayout layout{};
};
struct LegacyContext {
    const BoneMatrix* source{}; uint32_t generation{};
    int slot{}, count{}, cameraControl{}, wrist{}, lWrist{};
};
static ReachContext g_reachFpInterpolations[2];
static LegacyContext g_fpInterpolationContexts[2];
static int nodeCount = 5;
static bool reads = true, descriptor = true;
static bool SafeReadBytes(const void* src, void* dst, size_t size)
{ if (!reads || !src) return false; std::memcpy(dst, src, size); return true; }
static bool ReachResolvePaletteNodeCount(uint16_t, int& count)
{ count = nodeCount; return descriptor; }
static int LegacyAnatomicalRenderNodeCount(GameTitle, uint16_t) { return nodeCount; }
static uint64_t GetTickCount64() { return 1000; }
#include "reach_modded_fp_fixture.inl"
#include "reach_modded_fp_assets.inl"

static void AssetTests()
{
    BoneMatrix bones[120]{};
    for (const auto& asset : assets)
    {
        ReachFpBodyLayout layout{};
        Check(ResolveReachFpBodyLayout(asset.body, asset.count, layout, 0x19021311u),
              "All inspected weapon graphs admit their verified Elite body mapping");
        Check(layout.leftWristSource == asset.left && layout.rightWristSource == asset.right &&
              layout.cameraControlSource == asset.camera, "Anchors match independent graph names");
        const std::span<const int32_t> weapon(asset.weapon.data(), size_t(asset.weaponCount));
        FpHeldSources held{};
        Check(ResolveReachFpHeldSources(layout, weapon, held), "Native weapon map resolves");
        for (int i = -1; i <= asset.count; ++i)
        {
            const bool expected = std::find(weapon.begin(), weapon.end(), i) != weapon.end();
            Check(FpSourceIndexIsHeldObject(i, asset.count, 41, held) == expected,
                  "Only this model's outputs are carried, scaled and kept visible");
            if (i < 0 || i >= asset.count) continue;
            const bool body = std::find(asset.body.begin(), asset.body.end(), i) != asset.body.end();
            for (bool left : {false, true})
            {
                bool descendant = false;
                int ancestor = i;
                for (int depth = 0; ancestor >= 0 && depth < asset.count; ++depth)
                {
                    if (ancestor == (left ? asset.left : asset.right)) { descendant = body; break; }
                    ancestor = asset.parents[ancestor];
                }
                const auto mask = left ? layout.leftHandSourceDescendants : layout.rightHandSourceDescendants;
                Check(bool(mask & (uint64_t{1} << i)) == descendant,
                      "Rendered hand ownership agrees with graph ancestry, excluding gun and extra fingers");
            }
        }
        // Exercise the production native classifier with each real model.
        g_reachFpInterpolations[0] = {bones, 7, asset.count, layout};
        nodeCount = asset.weaponCount;
        Check(LegacyClassifyRuntimeWeapon(GameTitle::HaloReach, 1, 7, bones, weapon.data()),
              "Production classifier accepts the real interleaved/appended model");
        for (int body : asset.body)
        {
            auto wrong = asset.weapon; wrong[0] = body;
            Check(!LegacyClassifyRuntimeWeapon(GameTitle::HaloReach, 1, 7, bones, wrong.data()),
                  "A rendered arm/finger source cannot be misclassified as a gun");
        }
        Check(!LegacyClassifyRuntimeWeapon(GameTitle::HaloReach, 1, 8, bones, weapon.data()) &&
              !LegacyClassifyRuntimeWeapon(GameTitle::HaloReach, 1, 7, bones + 1, weapon.data()),
              "Classifier requires generation and exact source-pointer ownership");
        reads = false;
        Check(!LegacyClassifyRuntimeWeapon(GameTitle::HaloReach, 1, 7, bones, weapon.data()), "Unreadable remap is refused");
        reads = true; descriptor = false;
        Check(!LegacyClassifyRuntimeWeapon(GameTitle::HaloReach, 1, 7, bones, weapon.data()), "Unreadable descriptor is refused");
        descriptor = true;
        FpHeldSources bodyOnly{}; bodyOnly.explicitMap = true;
        for (int i = 0; i < asset.count; ++i)
            Check(!FpSourceIndexIsHeldObject(i, asset.count, 41, bodyOnly),
                  "Body palettes never infer held parts from unused graph nodes");
    }
}

static void GuardAndLifecycleTests()
{
    const auto& asset = assets[0];
    ReachFpBodyLayout layout{}, other{};
    Check(!ResolveReachFpBodyLayout(asset.body, asset.count, layout), "Custom maps require body identity");
    Check(!ResolveReachFpBodyLayout(asset.body, asset.count, layout, 0x181E0B17u), "Wrong body checksum refuses");
    Check(ResolveReachFpBodyLayout(asset.body, asset.count, layout, 0x19021311u), "Verified Carbine body");
    auto changed = asset.body;
    std::swap(changed[33], changed[34]);
    Check(ResolveReachFpBodyLayout(changed, asset.count, other, 0x19021311u) &&
          layout.bodySourceMask == other.bodySourceMask && !ReachLayoutsEqual(layout, other),
          "Full fingerprint detects changes even with identical body membership");
    Check(ReachLayoutsEqual(layout, layout), "Identical layouts remain equal");
    for (int bad : {-1, 0, asset.count, 64, 120})
    {
        auto invalid = asset.body; invalid[1] = bad;
        other = layout;
        Check(!ResolveReachFpBodyLayout(invalid, asset.count, other, 0x19021311u) && !other.Valid(),
              "Invalid maps clear stale layouts");
    }
    Check(!ResolveReachFpBodyLayout(asset.body, 40, other, 0x19021311u) &&
          !ResolveReachFpBodyLayout(asset.body, 121, other, 0x19021311u), "Source counts stay bounded");
    auto high = asset.body; high[40] = 63;
    Check(ResolveReachFpBodyLayout(high, 120, other, 0x19021311u), "Body bit 63 and 120-node source supported");
    FpHeldSources held{};
    const std::array<int32_t,3> highWeapon{64, 100, 119};
    Check(ResolveReachFpHeldSources(other, highWeapon, held), "Held outputs beyond bit 63 supported");
    Check(!FpSourceIndexIsHeldObject(63,120,41,held) &&
          FpSourceIndexIsHeldObject(119,120,41,held) &&
          !FpSourceIndexIsHeldObject(118,120,41,held), "No inferred ownership of holes");
    high[40] = 64;
    Check(!ResolveReachFpBodyLayout(high,120,other,0x19021311u), "Anatomical mask capacity remains guarded");
    for (const std::array<int32_t,2> invalid : {std::array<int32_t,2>{25,25}, {-1,25}, {25,46}, {25,0}})
    {
        Check(!ResolveReachFpHeldSources(layout, invalid, held) && held.explicitMap &&
              held.bits == std::array<uint64_t,2>{}, "Bad model remaps clear stale held ownership");
    }
    Check(!ResolveReachFpHeldSources(layout, {}, held), "Empty model stays unowned");
    for (int count : {48, 65, 120})
    {
        ReachFpBodyLayout elite{}, spartan{};
        Check(ResolveReachFpBodyLayout(kReachEliteFpBodyBoneMap, count, elite) &&
              ResolveReachFpBodyLayout(kReachSpartanFpBodyBoneMap, count, spartan),
              "Original fingerprints retain their acceptance path");
        for (int i = -1; i <= count; ++i)
        {
            Check(FpSourceIndexIsHeldObject(i,count,41,{}) == (i >= 41 && i < count),
                  "Legacy appended-range behavior remains unchanged");
            Check(!FpSourceIndexIsHeldObject(i,count,-1,{}), "Legacy disabled ownership remains disabled");
        }
    }
    Check(DecideReachFpPaletteAction(true,false,false,true,false,false) == ReachFpPaletteAction::LearnStockOnly &&
          DecideReachFpPairLayout(true,false,7,10,7,10) == ReachFpPairLayoutDecision::Stock &&
          DecideReachFpPairLayout(true,false,7,10,7,11) == ReachFpPairLayoutDecision::Active,
          "Discovery waits for the next prepared stereo pair");
    Check(DecideReachFpPaletteAction(true,true,true,true,false,true) == ReachFpPaletteAction::RestoreStockAndInvalidate,
          "Body mapping switches restore and invalidate");
}

static void LegacyRegressionTests()
{
    BoneMatrix bones[10]{};
    const int32_t body[]{0,1,2,3};
    const int32_t gun[]{4,5};
    const int32_t camera[]{4,9};
    for (GameTitle title : {GameTitle::Halo3, GameTitle::Halo3ODST})
    {
        g_fpInterpolationContexts[0] = {bones,17,0,10,9,2,3};
        nodeCount = 4;
        Check(!LegacyClassifyRuntimeWeapon(title,1,17,bones,body), "Legacy body is learned, not a gun");
        nodeCount = 2;
        Check(LegacyClassifyRuntimeWeapon(title,2,17,bones,gun), "Legacy appended weapon still classifies");
        Check(!LegacyClassifyRuntimeWeapon(title,2,17,bones,camera), "Legacy camera cannot become a gun");
        Check(!LegacyClassifyRuntimeWeapon(title,2,18,bones,gun), "Legacy stale generation still refused");
    }
}

int main()
{
    AssetTests(); GuardAndLifecycleTests(); LegacyRegressionTests();
    std::printf("Reach modded FP: %u checks passed\n", checks);
}
