#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../src/common/halo3_live_hud_layout.h"
#include <cassert>
#include <limits>
int main() {
    using namespace halo3_live_hud;
    Record r{};r.flags=1;r.fov=.034906585f;r.aspect=1.2f;r.depth=-.95f;r.width=1280;r.height=720;
    r.fields[0]=55;r.fields[1]=661;r.fields[2]=58;r.fields[3]=4;
    const Record authored=r;Record out{};
    assert(Transform(r,.6f,.7f,.8f,out));
    assert(out.fields[4]==.6f&&out.fields[5]==.7f&&std::fabs(out.depth+1.13f)<1e-5f);
    assert(std::memcmp(&r,&authored,sizeof(r))==0);
    Record twice{};assert(Transform(r,.6f,.7f,.8f,twice));assert(std::memcmp(&out,&twice,sizeof(out))==0);
    assert(Transform(r,.87f,.87f,.5f,out));assert(std::fabs(out.depth-r.depth)<1e-5f);
    // Every byte except the three intended fields is preserved.
    auto expected=r;expected.depth=out.depth;expected.fields[4]=.87f;expected.fields[5]=.87f;
    assert(std::memcmp(&expected,&out,sizeof(out))==0);
    r.fields[4]=r.fields[5]=.87f;assert(!Transform(r,.6f,.7f,.8f,out)); // accepted stock path
    r.width=1600;r.height=580;r.fields[1]=520;assert(Transform(r,.6f,.7f,.8f,out));
    r.width=960;assert(Transform(r,.6f,.7f,.8f,out));
    r.width=0;assert(!Transform(r,.6f,.7f,.8f,out));r.width=960;
    r.depth=std::numeric_limits<float>::quiet_NaN();assert(!Transform(r,.6f,.7f,.8f,out));r.depth=-.95f;
    assert(!Transform(r,.6f,.7f,2,out));r.fields[10]=std::numeric_limits<float>::infinity();
    assert(!Transform(r,.6f,.7f,.8f,out));
}
