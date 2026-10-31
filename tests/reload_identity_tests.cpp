#include "../src/common/legacy_weapon_visual_defaults.h"
#include <Windows.h>
#include "../src/common/weapon_model_catalog.h"
#include "../src/common/halo2_world_collision_logic.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <limits>

struct {bool manual_reload=true;bool per_gun_alignment=false;bool gun_barrel_aim=false;bool shots_from_muzzle=false;} g_config;
// The shot switches as they are in effect (sight_layer.h); here, the test's own config.
[[maybe_unused]] static bool Sight_ShotsFromMuzzle(){return g_config.shots_from_muzzle;}
[[maybe_unused]] static bool Sight_GunBarrelAim(){return g_config.gun_barrel_aim;}
struct BoneMatrix {float unused;};
struct Context {const BoneMatrix* source{};uint32_t generation{};bool valid{};} g_fpInterpolationContexts[2];
uint32_t observedGeneration{},observations{},fixtureChecksum{};
uint64_t observedIdentity{};
GameTitle observedTitle{},readTitle{};
bool fixtureHeld{},fixtureRead=true;
bool LegacyReadWeaponRenderModelChecksum(GameTitle title,uint16_t,uint32_t& result)
{readTitle=title;result=fixtureChecksum;return fixtureRead;}
bool LegacyClassifyRuntimeWeapon(GameTitle,uint16_t,uint32_t,const BoneMatrix*,const int32_t*)
{return fixtureHeld;}
void VR_ObserveWeaponModel(GameTitle title,uint32_t generation,uint64_t identity) noexcept
{++observations;observedTitle=title;observedGeneration=generation;observedIdentity=identity;}
// This fixture extracts the production observer bodies but deliberately does not
// exercise the separate secondary-presentation path.
void VR_ObserveSecondaryWeaponModel(GameTitle,uint32_t,uint64_t) noexcept {}
uint64_t visualIdentities[2]{};
void VR_ObserveHalo2VisualModel(bool secondary,uint32_t,uint64_t identity) noexcept
{visualIdentities[secondary?1:0]=identity;}
uint32_t TitleAdapter_GetGeneration(GameTitle) {return 7;}
using Halo2GraphDefinitionGetFn=const void*(__fastcall*)(uint32_t);
std::atomic<uintptr_t> g_graphDefinitionGet{},g_halo2TagDataBaseSlot{};
unsigned char definition[0x1C]{},tagData[128]{};
unsigned char* tagBase=tagData;
const void* __fastcall GetDefinition(uint32_t) {return definition;}
#include "reload_identity_functions.inl"
unsigned checks{},failures{};
void Check(bool ok,const char* message)
{++checks;if(!ok){++failures;std::printf("FAIL: %s\n",message);}}
int main()
{
    // Every title's Needler, Plasma Pistol and plasma rifles aim one-handed by name, and nothing else does; the Needle
    // Rifle is a long gun, and a plasma cannon or launcher is not a plasma rifle.
    for(const auto& model:weapon_model::kModels)
    {
        const char* const name=model.name;
        const bool plasmaHandgun=strstr(name,"plasma_pistol")||strstr(name,"plasma pistol")||
            strstr(name,"plasma_rifle")||strstr(name,"plasma rifle");
        Check(weapon_model::AlwaysOneHandedAim(&model)==(strstr(name,"needler")!=nullptr||plasmaHandgun),
              "only a Needler, Plasma Pistol or plasma rifle always aims one-handed");
    }
    {
        int ce=0,brute=0,red=0;
        for(const auto& model:weapon_model::kModels)
        {
            if(!weapon_model::AlwaysOneHandedAim(&model))continue;
            ce+=model.title==GameTitle::HaloCE&&strstr(model.name,"plasma")?1:0;
            brute+=strcmp(model.name,"fp_brute_plasma_rifle")==0?1:0;
            red+=strcmp(model.name,"fp_plasma_rifle_red")==0?1:0;
        }
        Check(ce==2&&brute>0&&red>0,"Halo CE's two, the Brute's and ODST's red one are all covered");
    }
    Check(!weapon_model::AlwaysOneHandedAim(nullptr),"an unknown weapon follows the range");
    BoneMatrix primary{},secondary{};int32_t remap=1;
    g_fpInterpolationContexts[0]={&primary,7,false}; // already consumed body receipt
    g_fpInterpolationContexts[1]={&secondary,7,false};
    for(const auto& model:weapon_model::kModels)
        if(model.title==GameTitle::Halo3||model.title==GameTitle::Halo3ODST||model.title==GameTitle::HaloReach)
        {
            fixtureChecksum=static_cast<uint32_t>(model.identity);fixtureHeld=false;
            observations=0;LegacyObserveReloadModel(model.title,123,7,&primary,&remap);
            Check(observations==1&&observedIdentity==model.identity&&observedTitle==model.title&&
                readTitle==model.title&&observedGeneration==7,"production observer selects each title's own exact catalogue identity");
            g_config.manual_reload=false;observations=0;
            LegacyObserveReloadModel(model.title,123,7,&primary,&remap);
            Check(observations==1&&observedIdentity==model.identity,
                "primary weapon identity remains available for visual defaults when manual reload is disabled");
            g_config.per_gun_alignment=true;observations=0;
            LegacyObserveReloadModel(model.title,123,7,&primary,&remap);
            Check(observations==1&&observedIdentity==model.identity,
                "per-gun alignment retains primary identity with manual reload disabled");
            g_config.per_gun_alignment=false;g_config.manual_reload=true;
            observations=0;LegacyObserveReloadModel(model.title,123,7,nullptr,&remap);
            Check(!observations,"null native source refused");
            if(model.title!=GameTitle::HaloReach)
            {
                observations=0;LegacyObserveReloadModel(model.title,123,7,&secondary,&remap);
                Check(!observations,"secondary model cannot evict primary model");
                LegacyObserveReloadModel(model.title,123,8,&primary,&remap);
                Check(!observations,"foreign generation cannot observe stale interpolation bank");
            }
        }
    for(auto title:{GameTitle::Halo3,GameTitle::Halo3ODST,GameTitle::HaloReach})
    {
        fixtureChecksum=0xabcdef01;fixtureHeld=false;observations=0;
        LegacyObserveReloadModel(title,123,7,&primary,&remap);
        Check(!observations,"unfamiliar body/scene model cannot fabricate a generic magazine");
        fixtureHeld=true;LegacyObserveReloadModel(title,123,7,&primary,&remap);
        Check(observations==1&&observedIdentity==fixtureChecksum,"proven appended custom weapon publishes live identity");
        fixtureChecksum=0;LegacyObserveReloadModel(title,123,7,&primary,&remap);
        Check(observedIdentity==weapon_model::LiveIdentity(0,123),"custom model with absent import checksum uses bounded native tag identity");
        fixtureRead=false;observations=0;LegacyObserveReloadModel(title,123,7,&primary,&remap);
        Check(!observations,"unreadable native model refuses automatic detection");fixtureRead=true;
    }
    g_graphDefinitionGet=reinterpret_cast<uintptr_t>(GetDefinition);
    g_halo2TagDataBaseSlot=reinterpret_cast<uintptr_t>(&tagBase);
    int32_t count=1,offset=32;std::memcpy(definition+0x14,&count,4);std::memcpy(definition+0x18,&offset,4);
    for(const auto& model:weapon_model::kModels) if(model.title==GameTitle::Halo2)
    {
        float bounds[6]{};
        for(int axis=0;axis<3;++axis){bounds[axis*2]=model.minimum[axis];bounds[axis*2+1]=model.maximum[axis];}
        std::memcpy(tagData+offset,bounds,sizeof(bounds));observations=0;
        Halo2ObserveReloadModel(123,model.nodeCount);
        Check(observations==1&&observedTitle==GameTitle::Halo2&&observedGeneration==7,
            "production H2 observer reads verified +14/+18 compression header");
        if(model.vertexCount||model.needles) Check(observedIdentity==model.identity,
            "production H2 observer exactly identifies every drawable magazine and Needler");
        const auto visualIdentity=weapon_alignment::Halo2VisualIdentity(model.minimum,model.maximum,model.nodeCount);
        Check(visualIdentities[0]==visualIdentity,"primary publishes separate visual identity");
        const auto primaryIdentity=observedIdentity;observations=0;
        const auto secondaryIdentity=Halo2ObserveReloadModel(123,model.nodeCount,false);
        Check(!observations&&secondaryIdentity==primaryIdentity&&observedIdentity==primaryIdentity,
            "secondary muzzle model lookup cannot replace primary alignment/reload identity");
        Check(visualIdentities[1]==visualIdentity,"secondary publishes its own visual identity");
        g_config.manual_reload=false;g_config.gun_barrel_aim=true;
        Check(Halo2ObserveReloadModel(123,model.nodeCount,false)==primaryIdentity,
            "muzzle model lookup works independently of manual reload");
        g_config.manual_reload=true;g_config.gun_barrel_aim=false;
    }
    float custom[]{-.1f,.3f,-.05f,.1f,-.06f,.2f};std::memcpy(tagData+offset,custom,sizeof(custom));
    Halo2ObserveReloadModel(123,8);const uint64_t first=observedIdentity;
    Halo2ObserveReloadModel(124,8);Check(first&&observedIdentity&&first!=observedIdentity,
        "production H2 custom-weapon identities detect switches without a stock catalogue");
    custom[0]=std::numeric_limits<float>::quiet_NaN();std::memcpy(tagData+offset,custom,sizeof(custom));
    Halo2ObserveReloadModel(123,8);Check(!observedIdentity,"invalid H2 custom bounds invalidate old identity");
    count=0;std::memcpy(definition+0x14,&count,4);Halo2ObserveReloadModel(123,8);
    Check(!observedIdentity,"unavailable native compression record cannot reuse previous model");
    std::printf("Native reload observers: %u checks, %u failures\n",checks,failures);
    return failures?1:0;
}
