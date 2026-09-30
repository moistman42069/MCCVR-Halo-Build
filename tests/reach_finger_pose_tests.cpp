#include "../src/common/reach_finger_pose_logic.h"

#include <cmath>
#include <cstdio>
#include <cstring>

struct T { float scale; float rotation[9]; float translation[3]; };
static int checks=0,failures=0;
#define CHECK(x) do { ++checks; if(!(x)) { ++failures; std::printf("FAIL %s:%d: %s\n",__FILE__,__LINE__,#x); } } while(0)

template<size_t N>
static void MakePose(T (&pose)[N]) {
    for(size_t i=0;i<N;++i) {
        pose[i]={};pose[i].scale=1;
        pose[i].rotation[0]=pose[i].rotation[4]=pose[i].rotation[8]=1;
        pose[i].translation[0]=static_cast<float>(i+1);
    }
}

template<size_t N>
static bool Different(const T (&a)[N],const T (&b)[N],unsigned node) {
    return std::memcmp(&a[node],&b[node],sizeof(T))!=0;
}

template<size_t N>
static void Exercise(uint32_t checksum,unsigned count) {
    T source[N]{},open[N]{},triggered[N]{},gripped[N]{},again[N]{};
    MakePose(source);
    const float down[3]{0,0,-1};
    const ControllerFingerInput none{true,0,0};
    const ControllerFingerInput trigger{true,1,0};
    const ControllerFingerInput grip{true,0,1};
    const unsigned side=0;
    CHECK(reach_fingers::Apply(checksum,count,side,source,down,none,open));
    CHECK(reach_fingers::Apply(checksum,count,side,source,down,trigger,triggered));
    CHECK(reach_fingers::Apply(checksum,count,side,source,down,grip,gripped));
    CHECK(std::memcmp(source,open,sizeof(source))==0);
    const unsigned index0=checksum==reach_fingers::kSpartanChecksum?24u:20u;
    const unsigned middle0=checksum==reach_fingers::kSpartanChecksum?25u:0u;
    CHECK(Different(open,triggered,index0));
    if(middle0) CHECK(!Different(open,triggered,middle0));
    CHECK(Different(open,gripped,index0)==false);
    if(middle0) CHECK(Different(open,gripped,middle0));
    CHECK(reach_fingers::Apply(checksum,count,side,source,down,trigger,again));
    CHECK(std::memcmp(triggered,again,sizeof(source))==0);
    ControllerFingerInput invalid{};
    CHECK(reach_fingers::Apply(checksum,count,side,source,down,invalid,again));
    CHECK(std::memcmp(source,again,sizeof(source))==0);
}

int main() {
    CHECK(reach_fingers::kSpartanNodeCount==47);
    CHECK(reach_fingers::kEliteNodeCount==41);
    CHECK(reach_fingers::FreeSupportPoseAdmitted(true,true,true,false,false,false,true,true));
    CHECK(!reach_fingers::FreeSupportPoseAdmitted(false,true,true,false,false,false,true,true));
    CHECK(!reach_fingers::FreeSupportPoseAdmitted(true,false,true,false,false,false,true,true));
    CHECK(!reach_fingers::FreeSupportPoseAdmitted(true,true,false,false,false,false,true,true));
    CHECK(!reach_fingers::FreeSupportPoseAdmitted(true,true,true,true,false,false,true,true));
    CHECK(!reach_fingers::FreeSupportPoseAdmitted(true,true,true,false,true,false,true,true));
    CHECK(!reach_fingers::FreeSupportPoseAdmitted(true,true,true,false,false,true,true,true));
    CHECK(!reach_fingers::FreeSupportPoseAdmitted(true,true,true,false,false,false,false,true));
    CHECK(!reach_fingers::FreeSupportPoseAdmitted(true,true,true,false,false,false,true,false));
    Exercise<reach_fingers::kSpartanNodeCount>(reach_fingers::kSpartanChecksum,
        reach_fingers::kSpartanNodeCount);
    Exercise<reach_fingers::kEliteNodeCount>(reach_fingers::kEliteChecksum,
        reach_fingers::kEliteNodeCount);
    T src[47]{},out[47]{};MakePose(src);const float down[3]{0,0,-1};
    const ControllerFingerInput input{true,1,1};
    CHECK(!reach_fingers::Apply(0,47,0,src,down,input,out));
    CHECK(!reach_fingers::Apply(reach_fingers::kSpartanChecksum,46,0,src,down,input,out));
    CHECK(!reach_fingers::Apply(reach_fingers::kSpartanChecksum,47,2,src,down,input,out));
    ControllerFingerInput nanInput{true,NAN,0};
    CHECK(!reach_fingers::Apply(reach_fingers::kSpartanChecksum,47,0,src,down,nanInput,out));
    const float badDown[3]{0,0,0};
    CHECK(!reach_fingers::Apply(reach_fingers::kSpartanChecksum,47,0,src,badDown,input,out));
    std::printf("%d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
