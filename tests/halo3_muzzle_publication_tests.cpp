#include <Windows.h>
#include "../src/common/weapon_muzzle.h"
#include "../src/common/contact_melee_motion.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
static unsigned checks{};
static void Check(bool value,const char* name)
{++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",name);std::exit(1);}}
static constexpr uint32_t owner=0x12340001,primary=0x56780002,secondary=0x789A0003;
static struct {bool gun_barrel_aim=true,left_handed=false;} g_config;
static std::atomic<bool> g_halo3MuzzleBindingsReady{true};
static std::atomic<uint32_t> g_halo3RuntimeGeneration{7};
static weapon_muzzle::Store g_halo3Muzzles;
static std::atomic<bool> g_baseCamValid{true};
static std::atomic<float> g_worldScale{1},g_baseCamX{0},g_baseCamY{0},g_baseCamZ{0};
static float g_headYawRef=0,g_gameYawRef=0,g_headPosRef[3]{};
static unsigned reticlePublications{};
static void VR_PublishWeaponReticleRay(GameTitle title,uint8_t slot,
    const weapon_muzzle::Receipt& receipt,const contact_melee::TrackingToWorld& transform) noexcept
{
    Check(title==GameTitle::Halo3&&slot<2&&transform.Valid(),
        "reticle receives title-local valid tracking/world transform");
    if(receipt.unit==owner&&receipt.weapon==(slot?secondary:primary)) ++reticlePublications;
}
static uint32_t tlsIndex=0;
static uint32_t* g_engineTlsIndex=&tlsIndex;
static alignas(8) uint8_t tlsBytes[0x600]{},users[0x2430]{};
static uint8_t* slots[]{tlsBytes};
static bool owned=true,seated=false;
static int32_t Player(int32_t){return int32_t(owner);}
static bool Seated(int32_t){return seated;}
static auto g_halo3PlayerUnitGetter=&Player;
static auto g_halo3UnitInVehicle=&Seated;
static bool Halo3ReadOwnedWeapons(uint32_t unit,uint32_t* weapons,bool requireDual)
{weapons[0]=primary;weapons[1]=secondary;return owned&&unit==owner&&!requireDual;}
struct BoneMatrix {float scale=1,rotation[9]{1,0,0,0,1,0,0,0,1},translation[3]{};};
struct FpInterpolationContext
{bool valid=true;int player=0,slot=0;uint32_t generation=7,muzzleUnit=owner,muzzleWeapon=primary;};
static struct
{
    bool armed=true;
    struct {uint64_t serial=11,referenceEpoch=3;int64_t timeNs=1000000000;struct {bool valid=true;} hands[2];} anatomicalTracking;
} g_fpStereoSolveScope;
static uint32_t liveChecksum=0;static int liveCount=0;
static int LegacyAnatomicalRenderNodeCount(GameTitle title,uint16_t tag,uint32_t* checksum)
{Check(title==GameTitle::Halo3&&tag==123,"title-local native render-model lookup");*checksum=liveChecksum;return liveCount;}
#define __readgsqword(offset) reinterpret_cast<uint64_t>(::slots)
#include "../src/dll/halo3_muzzle_publication.inl"
#undef __readgsqword
int main()
{
    const uint8_t* nativeUsers=users;std::memcpy(tlsBytes+0x568,&nativeUsers,sizeof(nativeUsers));
    std::memcpy(users+0x3C,&primary,4);std::memcpy(users+0x11BC+0x3C,&secondary,4);
    uint32_t unit=UINT32_MAX,weapon=UINT32_MAX;
    Check(Halo3CaptureMuzzleOwner(0,0,unit,weapon)&&unit==owner&&weapon==primary,"primary exact full native FP handle captured");
    Check(Halo3CaptureMuzzleOwner(0,1,unit,weapon)&&weapon==secondary,"secondary exact full native FP handle captured");
    Check(!Halo3CaptureMuzzleOwner(1,0,unit,weapon)&&!Halo3CaptureMuzzleOwner(0,2,unit,weapon),"foreign player and slot refused");
    BoneMatrix destination[64];
    for(auto& node:destination){node.scale=2;node.translation[0]=2;node.translation[1]=3;node.translation[2]=4;}
    for(const auto& marker:weapon_muzzle::kMarkers)
    {
        if(marker.title!=GameTitle::Halo3)continue;
        liveChecksum=uint32_t(marker.identity);liveCount=marker.nodeCount;
        for(int slot=0;slot<2;++slot)
        {
            FpInterpolationContext context;context.slot=slot;context.muzzleWeapon=slot?secondary:primary;
            Halo3PublishMuzzlePalette(123,context,destination,7);
            weapon_muzzle::Receipt sample{};weapon_muzzle::Ray expected{};
            Check(weapon_muzzle::Transform(marker,2,destination[marker.node].rotation,destination[marker.node].translation,expected),"authored H3 marker test setup");
            Check(g_halo3Muzzles.Read(GameTitle::Halo3,7,owner,context.muzzleWeapon,3,GetTickCount64(),1000000000,
                uint8_t(slot),marker.barrel,false,sample)&&sample.ray.position[0]==expected.position[0]&&sample.ray.up[2]==expected.up[2],
                "production H3 publisher uses final composed node, full slot and authored roll");
            Check(reticlePublications>0,"accepted authored muzzle reaches per-hand reticle publication");
            context.muzzleWeapon^=0x10000;
            Halo3PublishMuzzlePalette(123,context,destination,7);
            Check(!g_halo3Muzzles.Read(GameTitle::Halo3,7,owner,slot?secondary:primary,3,GetTickCount64(),1000000000,
                uint8_t(slot),marker.barrel,false,sample),"replaced interpolated weapon invalidates its old palette receipt");
        }
    }
    const uint32_t replaced=primary^0x10000;std::memcpy(users+0x3C,&replaced,4);
    Check(!Halo3CaptureMuzzleOwner(0,0,unit,weapon),"FP/inventory identity disagreement cannot bind a stale palette");
    std::memcpy(users+0x3C,&primary,4);
    owned=false;Check(!Halo3CaptureMuzzleOwner(0,0,unit,weapon),"foreign weapon ownership rejected");owned=true;
    seated=true;Check(!Halo3CaptureMuzzleOwner(0,0,unit,weapon),"seated muzzle stays native");seated=false;
    g_halo3MuzzleBindingsReady=false;
    Check(!Halo3CaptureMuzzleOwner(0,0,unit,weapon),"no FP read authority without independent native proof");
    std::printf("PASS: %u production H3 muzzle publication checks\n",checks);
}
