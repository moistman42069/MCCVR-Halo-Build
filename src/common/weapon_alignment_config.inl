// Included inside config.cpp's private namespace after the title descriptors.
// ConfigLoad/Save/profile switching serialize here; palette hooks only publish
// their existing bounded model observation and never enter this code.
struct WeaponAlignmentField
{
    const char* suffix;
    float Config::*live;
    float minimum, maximum;
};
constexpr WeaponAlignmentField kWeaponAlignmentFields[]{
    {"gun_scale",&Config::gun_scale,.3f,3.f},
    {"left_hand_scale",&Config::left_hand_scale,.3f,3.f},
    {"gun_pitch_deg",&Config::gun_pitch_deg,-180.f,180.f},
    {"gun_yaw_deg",&Config::gun_yaw_deg,-180.f,180.f},
    {"gun_roll_deg",&Config::gun_roll_deg,-180.f,180.f},
    {"gun_forward_m",&Config::gun_forward_m, weapon_alignment::kForwardMinimum, weapon_alignment::kForwardMaximum},
    {"gun_right_m",&Config::gun_right_m, weapon_alignment::kRightMinimum, weapon_alignment::kRightMaximum},
    {"gun_up_m",&Config::gun_up_m, weapon_alignment::kUpMinimum, weapon_alignment::kUpMaximum},
    {"left_gun_forward_m",&Config::left_gun_forward_m, weapon_alignment::kForwardMinimum, weapon_alignment::kForwardMaximum},
    {"left_gun_right_m",&Config::left_gun_right_m, weapon_alignment::kRightMinimum, weapon_alignment::kRightMaximum},
    {"left_gun_up_m",&Config::left_gun_up_m, weapon_alignment::kUpMinimum, weapon_alignment::kUpMaximum},
    {"left_hand_forward_m",&Config::left_hand_forward_m,-.15f,.3f},
    {"barrel_pitch_deg",&Config::barrel_pitch_deg,-180.f,180.f},
    {"barrel_yaw_deg",&Config::barrel_yaw_deg,-180.f,180.f},
    {"barrel_roll_deg",&Config::barrel_roll_deg,-180.f,180.f},
    {"halo2_classic_gun_pitch_deg",&Config::halo2_classic_gun_pitch_deg,-180.f,180.f},
    {"halo2_classic_gun_yaw_deg",&Config::halo2_classic_gun_yaw_deg,-180.f,180.f},
    {"halo2_classic_gun_roll_deg",&Config::halo2_classic_gun_roll_deg,-180.f,180.f},
    {"halo2_classic_gun_forward_m",&Config::halo2_classic_gun_forward_m,-.5f,.5f},
    {"halo2_classic_gun_right_m",&Config::halo2_classic_gun_right_m,-.3f,.3f},
    {"halo2_classic_gun_up_m",&Config::halo2_classic_gun_up_m,-.3f,.3f},
    {"muzzle_height_m",&Config::muzzle_height_m,-.3f,.3f},
};
constexpr size_t kWeaponAlignmentFieldCount=std::size(kWeaponAlignmentFields);
struct WeaponAlignmentProfile
{
    int title{};
    uint64_t identity{};
    float values[kWeaponAlignmentFieldCount]{};
    bool set[kWeaponAlignmentFieldCount]{};
};
std::recursive_mutex g_profileMutex;
std::vector<WeaponAlignmentProfile> g_weaponProfiles;
int g_activeWeaponProfile=-1;
uint64_t g_observedWeaponIdentity=0;
float g_weaponFallback[kWeaponAlignmentFieldCount]{};
std::atomic<const char*> g_activeWeaponName{nullptr};

const weapon_model::Model* AlignmentModel(int profile,uint64_t identity)
{
    constexpr GameTitle titles[]{GameTitle::Halo3,GameTitle::Halo3ODST,
        GameTitle::HaloReach,GameTitle::Halo4,GameTitle::Halo2,GameTitle::Halo2,GameTitle::HaloCE};
    return profile>=0&&profile<kTitleProfileCount
        ?weapon_model::Find(titles[profile],identity):nullptr;
}
int WeaponAlignmentFieldIndex(float Config::*field)
{
    for(size_t i=0;i<kWeaponAlignmentFieldCount;++i)
        if(kWeaponAlignmentFields[i].live==field) return static_cast<int>(i);
    return -1;
}
float FiniteAlignmentValue(float value,const WeaponAlignmentField& field,float fallback)
{ return std::isfinite(value)?std::clamp(value,field.minimum,field.maximum):fallback; }
void StoreWeaponAlignment()
{
    if(g_activeWeaponProfile<0) return;
    auto& profile=g_weaponProfiles[g_activeWeaponProfile];
    for(size_t i=0;i<kWeaponAlignmentFieldCount;++i)
    {
        const auto& field=kWeaponAlignmentFields[i];
        profile.values[i]=FiniteAlignmentValue(g_config.*field.live,field,g_weaponFallback[i]);
        profile.set[i]=true;
    }
}
void RestoreWeaponFallback()
{
    if(g_activeWeaponProfile<0) return;
    StoreWeaponAlignment();
    for(size_t i=0;i<kWeaponAlignmentFieldCount;++i)
        g_config.*kWeaponAlignmentFields[i].live=g_weaponFallback[i];
    g_activeWeaponProfile=-1;
    g_activeWeaponName.store(nullptr,std::memory_order_release);
}
float SharedAlignmentValue(float Config::*field)
{
    const int i=WeaponAlignmentFieldIndex(field);
    return g_activeWeaponProfile>=0&&i>=0?g_weaponFallback[i]:g_config.*field;
}
bool ParseWeaponAlignmentKey(const char* key,const char* val)
{
    if(strncmp(key,"weapon_alignment_",17)) return false;
    int title=-1;
    uint64_t identity=0;
    const char* end=key+strlen(key);
    const auto titleResult=std::from_chars(key+17,end,title);
    if(titleResult.ec!=std::errc{}||titleResult.ptr==end||*titleResult.ptr!='_') return false;
    const char* hex=titleResult.ptr+1;
    if(end-hex<17||hex[16]!='_') return false;
    const auto identityResult=std::from_chars(hex,hex+16,identity,16);
    if(identityResult.ec!=std::errc{}||identityResult.ptr!=hex+16||
        !AlignmentModel(title,identity)) return false;
    int field=-1;
    for(size_t i=0;i<kWeaponAlignmentFieldCount;++i)
        if(!strcmp(hex+17,kWeaponAlignmentFields[i].suffix)) field=static_cast<int>(i);
    if(field<0) return false;
    auto found=std::find_if(g_weaponProfiles.begin(),g_weaponProfiles.end(),
        [&](const auto& p){return p.title==title&&p.identity==identity;});
    if(found==g_weaponProfiles.end())
    {
        // Bounded by the catalog and seven title profiles, regardless of cfg size.
        g_weaponProfiles.push_back({title,identity});
        found=std::prev(g_weaponProfiles.end());
    }
    float value=g_config.*kWeaponAlignmentFields[field].live;
    if(!ParseFloatSetting(key,val,value)) return true;
    found->values[field]=FiniteAlignmentValue(value,kWeaponAlignmentFields[field],
        g_config.*kWeaponAlignmentFields[field].live);
    found->set[field]=true;
    return true;
}
