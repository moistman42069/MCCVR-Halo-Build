#include <Windows.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include "../src/common/visual_hand_config.h"
#include "../src/common/runtime_types.h"
#include "../src/common/anatomical_palette_logic.h"
#include "../src/common/halo2_render_logic.h"
#include "../src/common/reach_render_logic.h"
#include "../src/common/haloce_first_person_logic.h"
static unsigned checks=0;
static void Check(bool value,const char* name){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",name);std::exit(1);}}
static bool Near(float a,float b){return std::abs(a-b)<.000001f;}
struct BoneMatrix{float scale=1,rotation[9]{1,0,0,0,1,0,0,0,1},translation[3]{};};
static bool NormalizedBasis(const BoneMatrix& m,float* out){std::memcpy(out,m.rotation,36);return true;}
static bool SafeReadBytes(const void* from,void* to,size_t bytes){if(!from||!to)return false;std::memcpy(to,from,bytes);return true;}
static bool SafeWriteBytes(void* to,const void* from,size_t bytes){return SafeReadBytes(from,to,bytes);}
static std::atomic<float> g_worldScale{.33f};
static uint32_t checksum=504041493;static int modelCount=37;
static int LegacyAnatomicalRenderNodeCount(GameTitle,uint16_t,uint32_t* sum){*sum=checksum;return modelCount;}
static struct{struct{bool leftHanded=false,handAlignment=false;} anatomicalTracking;}g_fpStereoSolveScope;
struct FpInterpolationContext{
    bool valid=true;int count=42,slot=0,wrist=6,lWrist=5,shoulder=2,elbow=4,lShoulder=1,lElbow=3;
    uint64_t wristDescendants=(uint64_t{1}<<6)|(uint64_t{1}<<8)|(uint64_t{1}<<37),lWristDescendants=(uint64_t{1}<<5)|(uint64_t{1}<<7);
};
#include "../src/dll/visual_hand_palette.inl"
struct ReachFpInterpolationContext{
    bool valid=true;int liveSourceCount=52;ReachFpBodyLayout layout{};struct{bool handAlignment=false;}targets;
};
static bool ReachReadRenderModelIdentity(uint16_t,uint32_t& sum,int& count){sum=checksum;count=modelCount;return true;}
#include "../src/dll/reach_visual_hand_offset.inl"
struct Halo2VisibleConsumerContext{
    bool valid=true,handAlignment=false;uint32_t handsCount=8;float worldScale=.33f;
    const int32_t* handsRemap=nullptr;const int32_t* secondaryHandsRemap=nullptr;
    Halo2FirstPersonArmBinding binding{},secondaryBinding{};
};
#include "../src/dll/halo2_visual_hand_offset.inl"
static void Settings(){g_config=Config{};g_config.left_hand_mesh_x_m=.1f;g_config.right_hand_mesh_z_m=-.05f;}
static void CommonTests()
{
    const float basis[]{0,1,0,-1,0,0,0,0,1};float l[3]{},r[3]{};
    Check(visual_hand::Deltas(basis,basis,.33f,{.1f,0,0},{0,0,-.05f},false,false,l,r)&&Near(l[1],.033f)&&Near(r[2],-.0165f),"offsets rotate with each hand and use metres");
    float points[128][3]{};const auto read=[&](size_t n){return points[n];};const auto write=[&](size_t n,const float* p){std::memcpy(points[n],p,12);};
    Check(!visual_hand::Apply(64,1,1,l,r,read,write)&&points[0][1]==0,"ambiguous hand masks leave all nodes unchanged");
    points[2][0]=NAN;Check(!visual_hand::Apply(64,1,4,l,r,read,write)&&points[0][1]==0,"late invalid node cannot partially change a palette");points[2][0]=0;
    Check(visual_hand::ApplyClassified(80,l,r,[](size_t n){return n==79?1:n==65?2:0;},read,write)&&Near(points[79][1],.033f)&&Near(points[65][2],-.0165f),"H4 nodes beyond64 use bounded classification without bit shifts");
    Check(!visual_hand::Delta(basis,.33f,{NAN,0,0},l)&&!visual_hand::Delta(basis,.33f,{.21f,0,0},l),"invalid or out-of-range configuration refused");
}
static void LegacyTests()
{
    for(const auto title:{GameTitle::Halo3,GameTitle::Halo3ODST})for(int mode=0;mode<3;++mode)
    {
        Settings();checksum=title==GameTitle::Halo3?504041493:286525724;modelCount=37;
        g_fpStereoSolveScope.anatomicalTracking={mode!=0,mode==2};BoneMatrix palette[42]{},before[42]{};int32_t map[37]{};
        for(int i=0;i<37;++i)map[i]=i;palette[1].scale=palette[2].scale=palette[3].scale=palette[4].scale=.0001f;
        std::memcpy(before,palette,sizeof(palette));FpInterpolationContext context;
        g_worldScale=.45f;LegacyApplyVisualHandOffsets(title,123,map,context,palette);g_worldScale=.33f;
        const bool swapped=mode==1;
        Check(Near(palette[swapped?6:5].translation[0],.1f*.45f),"physical left offset follows released/anatomical handedness at the live world scale");
        Check(Near(palette[swapped?5:6].translation[2],-.05f*.45f),"physical right offset independent");
        Check(std::memcmp(palette[3].translation,palette[5].translation,12)==0&&
            std::memcmp(palette[4].translation,palette[6].translation,12)==0,"H3/ODST hidden elbow anchors remain attached to their own shifted hands");
        Check(std::memcmp(palette+37,before+37,5*sizeof(BoneMatrix))==0,"gun descendants outside verified body model remain byte-exact");
        Check(std::memcmp(palette,before,sizeof(BoneMatrix))==0,"camera/root mesh transform unchanged");
        std::memcpy(palette,before,sizeof(palette));checksum=123;LegacyApplyVisualHandOffsets(title,123,map,context,palette);
        Check(std::memcmp(palette,before,sizeof(palette))==0,"unknown body identity stays stock");
        g_config=Config{};checksum=title==GameTitle::Halo3?504041493:286525724;LegacyApplyVisualHandOffsets(title,123,map,context,palette);
        Check(std::memcmp(palette,before,sizeof(palette))==0,"zero defaults preserve whole native palette exactly");
    }
}
static void ReachTests()
{
    Settings();checksum=404622103;modelCount=47;ReachFpInterpolationContext context;
    Check(ResolveReachFpBodyLayout(std::span<const int32_t>{kReachSpartanFpBodyBoneMap},52,context.layout),"actual HREK body remap admitted");
    BoneMatrix palette[52]{},before[52]{};
    for(int node=0;node<47;++node)
    {
        const auto bit=uint64_t{1}<<kReachSpartanFpBodyBoneMap[node];
        if((kReachLeftControllerOwnedAuxiliarySourceMask|kReachRightControllerOwnedAuxiliarySourceMask)&bit)palette[node].scale=.0001f;
    }
    std::memcpy(before,palette,sizeof(palette));
    const auto savedLeft=VisualLeftHandOffset(g_config);const auto savedRight=VisualRightHandOffset(g_config);
    g_config=Config{};ReachApplyVisualHandOffsets(123,kReachSpartanFpBodyBoneMap.data(),context,palette);
    Check(std::memcmp(palette,before,sizeof(palette))==0,"Reach zero offsets preserve all body and held matrices");Settings();
    g_worldScale=.45f;ReachApplyVisualHandOffsets(123,kReachSpartanFpBodyBoneMap.data(),context,palette);g_worldScale=.33f;
    Check(Near(palette[14].translation[0],.1f*.45f)&&Near(palette[11].translation[2],-.05f*.45f),"Reach actual palette remap routes physical hands at the live world scale");
    Check(std::memcmp(palette+47,before+47,5*sizeof(BoneMatrix))==0,"Reach held nodes excluded");
    for(int node=0;node<47;++node)
    {
        const auto bit=uint64_t{1}<<kReachSpartanFpBodyBoneMap[node];
        if(kReachLeftControllerOwnedAuxiliarySourceMask&bit)Check(std::memcmp(palette[node].translation,palette[14].translation,12)==0,"Reach left hidden anchors follow visible left hand");
        if(kReachRightControllerOwnedAuxiliarySourceMask&bit)Check(std::memcmp(palette[node].translation,palette[11].translation,12)==0,"Reach right hidden anchors follow visible right hand");
    }
    int32_t bad[47]{};std::memcpy(bad,kReachSpartanFpBodyBoneMap.data(),sizeof(bad));bad[14]=0;std::memcpy(palette,before,sizeof(palette));
    ReachApplyVisualHandOffsets(123,bad,context,palette);Check(std::memcmp(palette,before,sizeof(palette))==0,"Reach foreign mapping refused");
}
static void Halo4Tests()
{
    Settings();BoneMatrix palette[kHalo4StormFpBodyNodeCount+4]{},before[kHalo4StormFpBodyNodeCount+4]{};
    std::memcpy(before,palette,sizeof(palette));g_config=Config{};Halo4ApplyVisualHandOffsets(palette,.33f,false,false);
    Check(std::memcmp(palette,before,sizeof(palette))==0,"H4 zero offsets preserve all80 body nodes and held record");Settings();Halo4ApplyVisualHandOffsets(palette,.33f,false,false);
    for(int node=0;node<kHalo4StormFpBodyNodeCount;++node)
    {
        const auto role=Halo4ClassifyFloatingNode(node);const bool left=role==Halo4FloatingNodeRole::LeftHand||role==Halo4FloatingNodeRole::CollapseAtLeftWrist;
        const bool right=role==Halo4FloatingNodeRole::RightHand||role==Halo4FloatingNodeRole::CollapseAtRightWrist;
        Check(Near(palette[node].translation[0],left?.033f:0)&&Near(palette[node].translation[2],right?-.0165f:0),"H4 own80-node hand/helper roles translated once");
    }
    Check(std::memcmp(palette+kHalo4StormFpBodyNodeCount,before+kHalo4StormFpBodyNodeCount,4*sizeof(BoneMatrix))==0,"H4 held record remains outside body adjustment");
}
static void Halo2Tests()
{
    for(bool dual:{false,true})for(int mode=0;mode<3;++mode)
    {
        Settings();g_config.left_handed=mode!=0;Halo2VisibleConsumerContext context;context.handAlignment=mode==2;
        int32_t map[]{0,1,2,3,4,5,6,7};context.handsRemap=map;context.secondaryHandsRemap=map;
        auto& b=context.binding;b.valid=true;b.count=8;b.leftWrist=1;b.rightWrist=2;b.leftSubtree=0xA;b.rightSubtree=0x14;
        b.leftArmAncestors=0x20;b.rightArmAncestors=0x40;
        context.secondaryBinding=b;float palette[8*kHalo2FirstPersonNodeFloats]{},before[8*kHalo2FirstPersonNodeFloats]{};
        for(int node=0;node<8;++node){Halo2FirstPersonTransform transform{};if(node==5||node==6)transform.scale=.0001f;Halo2WriteFirstPersonTransform(transform,palette+node*kHalo2FirstPersonNodeFloats);}
        std::memcpy(before,palette,sizeof(palette));Halo2ApplyVisualHandOffsets(context,palette,dual);
        const bool swapped=mode==1;Check(Near(palette[(swapped?2:1)*kHalo2FirstPersonNodeFloats+10],.033f),"H2 both renderers single/dual left physical offset");
        Check(Near(palette[(swapped?1:2)*kHalo2FirstPersonNodeFloats+12],-.0165f),"H2 right physical offset");
        Check(std::memcmp(palette,before,kHalo2FirstPersonNodeFloats*sizeof(float))==0,"H2 root remains exact");
        Check(std::memcmp(palette+5*kHalo2FirstPersonNodeFloats+10,palette+kHalo2FirstPersonNodeFloats+10,12)==0&&
            std::memcmp(palette+6*kHalo2FirstPersonNodeFloats+10,palette+2*kHalo2FirstPersonNodeFloats+10,12)==0,"H2 hidden arm anchors follow correct hand in single/dual and handed modes");
        std::memcpy(palette,before,sizeof(palette));g_config=Config{};Halo2ApplyVisualHandOffsets(context,palette,dual);
        Check(std::memcmp(palette,before,sizeof(palette))==0,"H2 zero offsets preserve entire packet");
    }
}
static void CeTests()
{
    using namespace halo_ce;FirstPersonBinding binding;binding.count=8;binding.leftWrist=1;binding.rightWrist=2;binding.gun=7;
    binding.leftMask=0xA;binding.rightMask=0x94;binding.gunMask=0x80;binding.armMask[0]=0x20;binding.armMask[1]=0x40;
    for(int mode=0;mode<3;++mode)
    {
        ControllerRig rig;rig.visualLeftHandOffset[0]=.1f;rig.visualRightHandOffset[2]=-.05f;rig.leftHanded=mode!=0;rig.handAlignment=mode==2;
        NodeMatrix palette[8]{},before[8]{};std::memcpy(before,palette,sizeof(palette));
        Check(ApplyVisibleFirstPersonHandOffsets(binding,.33f,rig,palette),"CE actual visual offset helper accepts owned binding");
        const bool swapped=mode==1;Check(Near(palette[swapped?2:1].position.x,.033f)&&Near(palette[swapped?1:2].position.z,-.0165f),"CE physical hand routing preserved");
        Check(std::memcmp(palette+7,before+7,sizeof(NodeMatrix))==0&&std::memcmp(palette,before,sizeof(NodeMatrix))==0,"CE nested gun and Saber object root stay byte-exact");
        Check(Near(palette[5].position.x,palette[1].position.x)&&Near(palette[6].position.z,palette[2].position.z),"CE collapsed weighted forearms follow their shifted wrists");
        std::memcpy(palette,before,sizeof(palette));rig.visualLeftHandOffset[0]=rig.visualRightHandOffset[2]=0;
        Check(ApplyVisibleFirstPersonHandOffsets(binding,.33f,rig,palette)&&std::memcmp(palette,before,sizeof(palette))==0,"CE zero offsets preserve entire graph");
    }
}
static void ConfigTests()
{
    const auto path=std::filesystem::temp_directory_path()/(L"mccvr-hand-offset-fixture-"+std::to_wstring(GetCurrentProcessId())+L".cfg");
    {std::ofstream f(path);f<<"left_hand_mesh_x_m = 0.04\nright_hand_mesh_z_m = -0.02\nhalo3_left_hand_mesh_x_m = 0.07\nleft_gun_forward_m = -0.12\nhalo3_left_gun_forward_m = 0.07\nodst_left_gun_forward_m = -0.12\n";}
    ConfigLoad(path.c_str());Config_ApplyTitleProfile(0);
    Check(Near(g_config.left_hand_mesh_x_m,.07f)&&Near(g_config.right_hand_mesh_z_m,-.02f),"per-title overrides and shared migration defaults load");
    Check(Near(g_config.left_gun_forward_m,.07f)&&Near(g_config.title_profiles[1].left_gun_forward_m,-.12f),
        "per-title weapon offset overrides load independently");
    g_config.left_hand_mesh_y_m=.03f;g_config.right_hand_mesh_x_m=-.06f;
    g_config.left_gun_up_m=.03f;g_config.gun_up_m=-.06f;ConfigSave();ConfigLoad(path.c_str());
    Check(Near(g_config.left_hand_mesh_y_m,.03f)&&Near(g_config.right_hand_mesh_x_m,-.06f),"live hand sliders persist independently");
    Check(Near(g_config.left_gun_up_m,.03f)&&Near(g_config.gun_up_m,-.06f),
        "independent left/right weapon offsets persist separately");
    Config_ApplyTitleProfile(1);
    Check(Near(g_config.left_hand_mesh_x_m,.04f)&&g_config.left_hand_mesh_y_m==0,"title transition restores own hand offsets");
    Check(Near(g_config.left_gun_forward_m,-.12f),"title transition uses its own weapon offset");
    Config_ApplyTitleProfile(-1);std::filesystem::remove(path);
}
int main(){CommonTests();LegacyTests();ReachTests();Halo4Tests();Halo2Tests();CeTests();ConfigTests();std::printf("PASS: %u visual-hand production/config checks\n",checks);}
