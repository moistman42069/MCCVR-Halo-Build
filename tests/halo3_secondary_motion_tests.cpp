#ifdef NDEBUG
#undef NDEBUG
#endif
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include "../src/common/weapon_alignment.h"
#include "../src/common/weapon_animation_motion.h"

// Native API stand-ins exercise the production motion/effect code without MCC.
struct BoneMatrix {float scale=1,rotation[9]{1,0,0,0,1,0,0,0,1},translation[3]{};};
struct FpInterpolationContext {
    bool valid=true;
    int slot=1,player=0,count=4,lWrist=0;
    uint32_t generation=1,muzzleUnit=7,muzzleWeapon=9;
};
enum class GameTitle {Halo3,Other};
GameTitle title=GameTitle::Halo3;
GameTitle TitleAdapter_GetActiveTitle(){return title;}
uint64_t tick=1000;
uint64_t GetTickCount64(){return tick;}
FpInterpolationContext g_fpInterpolationContexts[2];
BoneMatrix g_fpUnmodifiedInterpolations[2][64];
struct {bool left_handed=false;} g_config;
std::atomic<float> g_worldScale{.33f},g_camX{},g_camY{},g_camZ{};
std::atomic<bool> g_camValid{true};
bool LoadCachedRenderRoot(int,BoneMatrix& out){out=BoneMatrix{};return true;}
bool LoadCameraBasis(float* out){BoneMatrix m;memcpy(out,m.rotation,sizeof(m.rotation));return true;}
bool ComposeBoneMatrices(const BoneMatrix& a,const BoneMatrix& b,BoneMatrix& out)
{
    BoneMatrix c;c.scale=a.scale*b.scale;
    for(int r=0;r<3;++r){
        c.translation[r]=a.translation[r];
        for(int k=0;k<3;++k)c.translation[r]+=a.scale*a.rotation[k*3+r]*b.translation[k];
        for(int col=0;col<3;++col){
            c.rotation[col*3+r]=0;
            for(int k=0;k<3;++k)c.rotation[col*3+r]+=a.rotation[k*3+r]*b.rotation[col*3+k];
        }
    }
    out=c;return true;
}
bool InvertBoneMatrix(const BoneMatrix& a,BoneMatrix& out)
{
    if(!std::isfinite(a.scale)||a.scale<.001f)return false;
    BoneMatrix b;b.scale=1/a.scale;
    for(int r=0;r<3;++r)for(int c=0;c<3;++c)b.rotation[c*3+r]=a.rotation[r*3+c];
    for(int r=0;r<3;++r)for(int k=0;k<3;++k)b.translation[r]-=b.rotation[k*3+r]*a.translation[k]/a.scale;
    out=b;return true;
}
#include "../src/dll/halo3_secondary_motion.inl"
bool near(float a,float b){return std::fabs(a-b)<.00001f;}
void same(const BoneMatrix& a,const BoneMatrix& b)
{
    assert(near(a.scale,b.scale));
    for(int i=0;i<9;++i)assert(near(a.rotation[i],b.rotation[i]));
    for(int i=0;i<3;++i)assert(near(a.translation[i],b.translation[i]));
}
int main()
{
    auto& context=g_fpInterpolationContexts[1];
    auto* original=g_fpUnmodifiedInterpolations[1];
    original[0].translation[0]=2;
    for(int i=0;i<17;++i){CaptureH3SecondaryMotion(context,original);tick+=100;}
    BoneMatrix target;target.translation[2]=3;
    BoneMatrix moved=target,neutral=target;
    ApplyH3SecondaryMotion(moved,neutral);same(moved,target);
    // Wrist rotation survives while controller position remains unchanged.
    tick+=16;original[0].translation[0]+=.1f;
    const float angle=.2f;
    original[0].rotation[0]=original[0].rotation[4]=std::cos(angle);
    original[0].rotation[1]=std::sin(angle);original[0].rotation[3]=-std::sin(angle);
    CaptureH3SecondaryMotion(context,original);
    ApplyH3SecondaryMotion(moved,neutral);
    assert(near(moved.translation[0],0.f)&&near(moved.translation[2],3));
    assert(moved.rotation[1]>0 && moved.rotation[1]<std::sin(angle));same(moved,neutral);
    const float filteredRotation=moved.rotation[1];
    // Production consumes the live receipt BEFORE the palette solver. The
    // validated local receipt must still apply motion, without rearming it.
    const auto consumed=context;
    context.valid=false;
    moved=target;neutral=target;
    ApplyH3SecondaryMotion(moved,neutral);same(moved,target);
    {
        H3SecondaryVisualScope scope(consumed);
        ApplyH3SecondaryMotion(moved,neutral);
        assert(near(moved.translation[0],0.f));
        assert(near(moved.rotation[1],filteredRotation));
        assert(!context.valid);
        auto primary=consumed;primary.slot=0;
        {
            H3SecondaryVisualScope nested(primary);
            moved=target;ApplyH3SecondaryMotion(moved,neutral);same(moved,target);
        }
        moved=target;ApplyH3SecondaryMotion(moved,neutral);
        assert(near(moved.translation[0],0.f));
    }
    moved=target;ApplyH3SecondaryMotion(moved,neutral);same(moved,target);
    context=consumed;
    // Other titles, stale samples and a different owner never inherit the delta.
    title=GameTitle::Other;moved=target;ApplyH3SecondaryMotion(moved,neutral);same(moved,target);
    title=GameTitle::Halo3;tick+=101;ApplyH3SecondaryMotion(moved,neutral);same(moved,target);
    ++context.muzzleWeapon;CaptureH3SecondaryMotion(context,original);
    ApplyH3SecondaryMotion(moved,neutral);same(moved,target);
    ++context.generation;CaptureH3SecondaryMotion(context,original);
    ApplyH3SecondaryMotion(moved,neutral);same(moved,target);
    original[0].translation[0]=std::numeric_limits<float>::quiet_NaN();
    CaptureH3SecondaryMotion(context,original);assert(!g_h3SecondaryMotion.valid);

    // Render remap selects only weapon nodes. Both effect paths must match the
    // visual correction, without accumulating it when the same buffer is reused.
    original[0]=BoneMatrix{};original[1]=BoneMatrix{};original[2]=BoneMatrix{};
    original[1].translation[0]=.4f;original[2].translation[1]=.5f;
    auto& binding=g_h3SecondaryEffectBinding;
    binding.generation=context.generation;binding.unit=context.muzzleUnit;
    binding.weapon=context.muzzleWeapon;binding.mask=uint64_t{1}<<1;
    binding.sequence=2;
    for(uint32_t checksum:{0x171C0B10u,0x16120C11u}) {
        binding.checksum=checksum;
        BoneMatrix output[4],root,frame;
        frame.translation[2]=2;
        output[2].translation[0]=42;
        BoneMatrix expected;
        ComposeBoneMatrices(frame,original[1],expected);
        for(int axis=0;axis<3;++axis)expected.translation[axis]=frame.translation[axis]+(expected.translation[axis]-frame.translation[axis])*1.2f;
        expected.scale*=1.2f;
        assert(H3CorrectSecondaryMatrix(frame,30,expected));
        ApplyH3SecondaryEffects(output,4,frame,&root,1.2f);same(output[1],expected);
        assert(output[2].translation[0]==42);
        ApplyH3SecondaryEffects(output,4,frame,&root,1.2f);same(output[1],expected);
        ApplyH3SecondaryEffects(output,4,frame,nullptr,1.2f);same(output[1],expected);
        const auto before=output[1];
        ++context.muzzleWeapon;ApplyH3SecondaryEffects(output,4,frame,nullptr,1.2f);same(output[1],before);
        --context.muzzleWeapon;
        original[1].translation[0]=std::numeric_limits<float>::quiet_NaN();
        ApplyH3SecondaryEffects(output,4,frame,nullptr,1.2f);same(output[1],before);
        original[1].translation[0]=.4f;
    }
    // The forward trim follows controller-forward, independently of
    // the preserved lateral trim. Both visible and muzzle palettes use this.
    BoneMatrix trimFrame{},trimValue{};
    assert(H3CorrectSecondaryMatrix(trimFrame,30,trimValue));
    assert(near(trimValue.translation[0],.015f*g_worldScale.load()));
    assert(near(trimValue.translation[1],.06f*g_worldScale.load()));

}
