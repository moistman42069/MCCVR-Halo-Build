#include "../src/common/legacy_weapon_visual_defaults.h"
#include "../src/common/halo4_weapon_visual_defaults.h"
#include "../src/common/reach_weapon_defaults.h"
#include <cstdio>
#include <limits>
using namespace weapon_alignment;
unsigned failures{};
void Check(bool ok,const char* message) { if(!ok){++failures;std::printf("FAIL: %s\n",message);} }
int main() {
    const auto* boltshot=weapon_model::Find(GameTitle::Halo4,0x1305160Bull);
    Check(boltshot&&Halo4DefaultVisualYaw(boltshot->identity)==3.8f,"Boltshot uses held stasis model and absolute 3.8 yaw");
    Check(Halo4DefaultVisualYaw(0x160F1B01ull)==0,"unrelated burst pistol retains neutral yaw");
    Check(Halo4DefaultVisualYaw(0x171C0B05ull)==-.7f&&Halo4DefaultVisualYaw(0x10030014ull)==3.f,"H4 values are absolute");
    Check(ReachWeaponDefaults(0x11091511ull).barrelPitch==-10.f,"Reach Needler requested barrel pitch remains -10 degrees");
    Check(OdstBarrelPitch(0x10190A19ull)==-10.f&&OdstBarrelPitch(0x17141007ull)==-8.f,"ODST Needler remains -10 degrees and Brute Shot remains -8");
    Check(LegacyWeaponVisualDefaults(GameTitle::Halo3ODST,0x150A000Cull).yaw==1.f&&
        LegacyWeaponVisualDefaults(GameTitle::Halo3,0x150A000Cull).yaw==-1.f,"ODST pistol correction preserves H3");
    const auto* rifle=weapon_model::Find(GameTitle::Halo2,0x1B55D3C632C37F00ull);
    Check(rifle&&!weapon_model::FindHalo2(rifle->minimum,rifle->maximum,rifle->nodeCount),"ambiguous H2 rifles still cannot select mesh");
    const auto shared=Halo2VisualIdentity(rifle->minimum,rifle->maximum,rifle->nodeCount);
    Check(shared==kHalo2SharedPlasmaRifleVisual&&LegacyWeaponVisualDefaults(GameTitle::Halo2,shared,true).yaw==1.f,
        "both H2 classic plasma rifles share only visual yaw");
    Check(!weapon_model::Find(GameTitle::Halo2,shared)&&
        weapon_model::Halo2LiveIdentity(rifle->minimum,rifle->maximum,rifle->nodeCount,123)!=shared,
        "visual alias never replaces reload or mesh identity");
    Check(LegacyWeaponVisualDefaults(GameTitle::Halo2,shared,false).yaw==0,"ambiguous H2A variants are not guessed");
    const auto* smg=weapon_model::Find(GameTitle::Halo2,0x67EC65EB1DE2A459ull);
    Check(Halo2VisualIdentity(smg->minimum,smg->maximum,smg->nodeCount)==smg->identity&&
        LegacyWeaponVisualDefaults(GameTitle::Halo2,smg->identity,true).yaw==-.7f,"H2 SMG identity and absolute yaw");
    Check(LegacyWeaponVisualDefaults(GameTitle::Halo2,0x1295A15AB1C5709Cull,true).yaw==1.5f&&
        LegacyWeaponVisualDefaults(GameTitle::Halo2,0xBEA57366DD3981D5ull,true).yaw==1.1f,"H2 rifle and shotgun final yaw");
    Check(LegacyWeaponVisualDefaults(GameTitle::Halo2,0x6D99DEAEE0C79FD5ull,false).yaw==-.5f&&
        LegacyWeaponVisualDefaults(GameTitle::Halo2,rifle->identity,false).yaw==.5f,"H2A latest yaw values");
    float low[]{0,0,0},high[]{1,1,1};
    Check(!Halo2VisualIdentity(low,high,5),"unknown model has no visual alias");
    low[0]=std::numeric_limits<float>::quiet_NaN();
    Check(!Halo2VisualIdentity(low,high,5),"invalid bounds have no visual alias");
    std::printf("Weapon defaults: %u failures\n",failures);
    return failures?1:0;
}
