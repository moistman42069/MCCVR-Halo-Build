#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../src/common/visual_aim.h"
#include "../src/common/weapon_alignment.h"
#include "../src/common/halo2_visual_aim.h"
#include <cassert>
#include <cstdio>
#include <limits>

static bool near(float a,float b) {return std::fabs(a-b)<.0001f;}
int main()
{
    using namespace weapon_alignment;
    assert(near(Forward(false,.12f,.03f),.12f));
    assert(near(Forward(true,.12f,.03f),.15f));
    assert(near(Right(false,.08f),.08f)&&near(Right(true,.08f),-.08f));
    assert(near(Up(.04f),.04f));
    const Offsets expectedRight[]{
        {-.16f,-.04f,-.06f},{-.16f,-.03f,-.05f},{-.16f,-.04f,-.06f},
        {-.20f,0,-.04f},{-.16f,.02f,-.04f},{-.16f,.02f,-.10f},{-.16f,.02f,-.05f}};
    const Offsets expectedLeft[]{
        {-.16f,.04f,0},{-.16f,.03f,-.01f},{-.16f,.04f,-.06f},
        {-.20f,-.02f,-.09f},{-.10f,0,-.04f},{-.12f,.01f,-.04f},{-.16f,-.02f,-.05f}};
    for (int profile=0;profile<7;++profile) {
        const auto right=Baseline(profile),left=expectedLeft[profile];
        assert(near(right.forward,expectedRight[profile].forward));
        assert(near(right.right,expectedRight[profile].right));
        assert(near(right.up,expectedRight[profile].up));
        assert(near(Forward(false,right.forward,0,profile),right.forward));
        assert(near(Right(false,right.right,profile),right.right));
        assert(near(Up(right.up,false,profile),right.up));
        assert(near(Forward(true,right.forward,0,profile),left.forward));
        assert(near(Right(true,right.right,profile),left.right));
        assert(near(Up(right.up,true,profile),left.up));
        assert(near(Forward(true,right.forward+.01f,.02f,profile),left.forward+.03f));
        assert(near(Right(true,right.right+.01f,profile),left.right-.01f));
        assert(near(Up(right.up+.01f,true,profile),left.up+.01f));
    }
    for(int profile=0;profile<7;++profile)for(bool left:{false,true}) {
        const auto base=left?kLeftOffsets[profile]:kRightOffsets[profile];
        for(float delta:{-.30f,0.f,.30f}) {
            assert(near(Forward(left,base.forward+delta,0.f),base.forward+delta));
            assert(near(Right(false,base.right+delta),base.right+delta));
            assert(near(Up(base.up+delta),base.up+delta));
        }
    }
    assert(near(kH2ClassicVisualYaw,-1.f));
    assert(H3SecondaryYaw(1,0x171C0B10u)==30 && H3SecondaryYaw(0,0x171C0B10u)==0);
    assert(H3SecondaryYaw(1,0x1E130811u)==0);
    assert(H3SecondaryYaw(1,0x16120C11u)==30 && H3SecondaryYaw(0,0x16120C11u)==0);
    assert(near(kH2ClassicVisualPitch,-9.5f));
    float frame[]{1,0,0,0,1,0,0,0,1},basis[]{1,0,0,0,1,0,0,0,1};
    float pivot[]{2,3,4},position[]{3,3,4};
    assert(RotateYaw(frame,pivot,30,basis,position));
    assert(near(basis[0],std::cos(.523598776f))&&near(basis[1],-.5f));
    assert(near(position[0]-pivot[0],basis[0])&&near(position[1]-pivot[1],basis[1]));
    for(float pitch:{-11.5f,0.f,30.f})for(float yaw:{-25.f,0.f,40.f})for(float roll:{-15.f,0.f,20.f}) {
        Halo2CameraBasis neutral{{2,3,4},{0,1,0},{0,0,1}},visual{};
        assert(Halo2ApplyCarrierTrim(neutral,pitch,yaw,roll,0,0,0,1,visual));
        float collision[]{.1f,-.2f,.05f};
        weapon_muzzle::Ray ray{};
        for(int i=0;i<3;++i) {
            ray.position[i]=visual.position[i]+visual.forward[i]*.3f+collision[i];
            ray.direction[i]=visual.forward[i];ray.up[i]=visual.up[i];
        }
        assert(Halo2RemoveVisualFrame(visual,neutral,collision,ray));
        for(int i=0;i<3;++i) {
            assert(near(ray.position[i],neutral.position[i]+neutral.forward[i]*.3f+collision[i]));
            assert(near(ray.direction[i],neutral.forward[i])&&near(ray.up[i],neutral.up[i]));
        }
        float from[9]{},to[9]{};
        for(int i=0;i<3;++i) {
            from[i]=visual.forward[i];from[6+i]=visual.up[i];
            to[i]=neutral.forward[i];to[6+i]=neutral.up[i];
            int j=(i+1)%3,k=(i+2)%3;
            from[3+i]=visual.up[j]*visual.forward[k]-visual.up[k]*visual.forward[j];
            to[3+i]=neutral.up[j]*neutral.forward[k]-neutral.up[k]*neutral.forward[j];
        }
        weapon_muzzle::Ray moved=ray;
        assert(Reframe(to,neutral.position,from,visual.position,moved));
        assert(Reframe(from,visual.position,to,neutral.position,moved));
        for(int i=0;i<3;++i) {
            assert(near(moved.position[i],ray.position[i]));
            assert(near(moved.direction[i],ray.direction[i]));
        }
        const auto saved=ray;
        visual.forward[0]=std::numeric_limits<float>::quiet_NaN();
        assert(!Halo2RemoveVisualFrame(visual,neutral,collision,ray));
        assert(std::memcmp(&saved,&ray,sizeof(ray))==0);
    }
    std::puts("Weapon alignment: mirrored offsets, scoped SMG yaw and H2 visual/aim separation passed");
}
