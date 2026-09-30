#include <windows.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include "../src/dll/odst_finger_pose_backend.inl"
#include "odst_finger_bind_fixture.h"
namespace {
using namespace odst_fingers;
unsigned checks=0,failures=0;
void Check(bool value,const char* name){++checks;if(!value){++failures;std::printf("FAIL: %s\n",name);}}
bool Near(const Matrix& a,const Matrix& b) {
    const auto* x=reinterpret_cast<const float*>(&a);const auto* y=reinterpret_cast<const float*>(&b);
    for(unsigned i=0;i<sizeof(Matrix)/sizeof(float);++i)if(std::fabs(x[i]-y[i])>2e-5f)return false;
    return true;
}
void Build(Pose& source,Pose& inverse) {
    Pose bind{};Matrix world{};world.translation[0]=8;world.translation[1]=-3;world.translation[2]=1.8f;
    for(unsigned node=0;node<Count;++node) {
        Check(Parents[node]==OdstFpBindParents[node],"own official ODST parent graph");
        Matrix local{};std::memcpy(local.translation,OdstFpBindLocal[node],12);
        Check(Halo4QuaternionToBlamBasis(OdstFpBindLocal[node]+3,local.rotation),"own official ODST default quaternion");
        if(Parents[node]>=0)Check(Halo4ComposeFloatingTransforms(bind[Parents[node]],local,bind[node]),"own local bind graph composed");
        else bind[node]=local;
        Check(Halo4InvertFloatingTransform(bind[node],inverse[node]),"own model default inverse derived");
        Check(Halo4ComposeFloatingTransforms(world,bind[node],source[node]),"native-style committed world palette");
    }
}
}
int main() {
    using namespace odst_fingers;
    Pose original{},inverse{};Build(original,inverse);
    for(uint32_t checksum:{286525724u,403178001u})for(unsigned side=0;side<2;++side) {
        Request request{};request.checksum=checksum;request.count=Count;
        Check(ConfigureFreeSupport(request,side==1,false,false,{true,0,0}),"unoccupied support accepts controller input");
        Check(request.allowed[side]&&!request.allowed[1-side],"actual anatomical route chooses one support hand");
        Pose result{};Check(Solve(original,inverse,request,result),"own ODST open-hand solve");
        for(unsigned node=0;node<Count;++node)Check(Near(result[node],original[node]),"zero curl reconstructs original own bind without moving wrist");
        request.fingers[side]={true,0,1};Pose pointing{};
        Check(Solve(original,inverse,request,pointing),"pointing gesture opens index and closes other fingers");
        for(unsigned joint=0;joint<3;++joint)Check(Near(pointing[Chains[side][0][joint]],original[Chains[side][0][joint]]),"unpressed trigger keeps pointing index open");
        for(unsigned finger=1;finger<5;++finger)Check(!Near(pointing[Chains[side][finger][1]],original[Chains[side][finger][1]]),"grip flexes other authored fingers");
        request.fingers[side]={true,1,1};Pose fist{};
        Check(Solve(original,inverse,request,fist),"own ODST closed fist");
        for(unsigned node=0;node<Count;++node) {
            if(IsFinger(node,side))Check(!Near(fist[node],original[node]),"all free finger joints respond");
            else Check(std::memcmp(&fist[node],&original[node],sizeof(Matrix))==0,"primary fingers wrists arms and gun carriers stay byte-identical");
        }
        Pose repeated{};Check(Solve(fist,inverse,request,repeated),"repeat committed palette can reapply pose");
        Check(std::memcmp(&fist,&repeated,sizeof(fist))==0,"controller curl never accumulates across eyes or repeat calls");
        request.fingers[side]={true,0,0};Pose reopened{};
        Check(Solve(fist,inverse,request,reopened),"release opens previously curled hand");
        for(unsigned node=0;node<Count;++node)Check(Near(reopened[node],original[node]),"release returns own default finger shape");
        const auto untouched=request;
        Check(!ConfigureFreeSupport(request,side==1,true,false,{true,1,1})&&std::memcmp(&request,&untouched,sizeof(request))==0,"secondary weapon preserves authored support grip");
        Check(!ConfigureFreeSupport(request,side==1,false,true,{true,1,1})&&std::memcmp(&request,&untouched,sizeof(request))==0,"persistent/two-hand attachment preserves authored fingers");
        Check(!ConfigureFreeSupport(request,side==1,false,false,{})&&std::memcmp(&request,&untouched,sizeof(request))==0,"unavailable raw input preserves current native shape");
        auto native=original;request.fingers[side]={true,.4f,.7f};
        Check(Apply(native.data(),inverse,request)==Result::Applied,"production committed-palette transaction applies");
        for(unsigned node=0;node<Count;++node)if(!IsFinger(node,side))Check(std::memcmp(&native[node],&original[node],sizeof(Matrix))==0,"transaction only writes free finger descendants");
        auto badInverse=inverse;badInverse[Chains[side][4][2]].scale=std::numeric_limits<float>::quiet_NaN();
        auto before=native;
        Check(Apply(native.data(),badInverse,request)==Result::Refused&&std::memcmp(&native,&before,sizeof(native))==0,"late invalid inverse bind never partially changes fingers");
        request.fingers[side].trigger=2;
        Check(Apply(native.data(),inverse,request)==Result::Refused&&std::memcmp(&native,&before,sizeof(native))==0,"invalid physical input refuses atomically");
        request.checksum=0;
        Check(Apply(reinterpret_cast<Matrix*>(uintptr_t(1)),inverse,request)==Result::NotApplicable,"unknown model does not touch native memory");
    }
    Request request{};request.checksum=286525724;request.count=Count;
    ConfigureFreeSupport(request,false,false,false,{true,.6f,.8f});
    Check(Apply(reinterpret_cast<Matrix*>(uintptr_t(1)),inverse,request)==Result::Faulted,"real palette read AV isolated");
    struct WithWeapon {Pose hands;std::array<uint8_t,256> appended;} combined{};
    combined.hands=original;combined.appended.fill(0xAB);const auto appended=combined.appended;
    Check(Apply(combined.hands.data(),inverse,request)==Result::Applied&&combined.appended==appended,"appended weapon and camera records untouched");
    SYSTEM_INFO system{};GetSystemInfo(&system);const size_t page=system.dwPageSize;
    auto* memory=static_cast<uint8_t*>(VirtualAlloc(nullptr,2*page,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    Check(memory!=nullptr,"allocate mixed-protection native palette fixture");
    if(memory) {
        auto* destination=reinterpret_cast<Matrix*>(memory+page-(18*sizeof(Matrix)+16));
        std::memcpy(destination,original.data(),sizeof(original));DWORD old=0;
        Check(VirtualProtect(memory+page,page,PAGE_READONLY,&old)!=0,"protect middle of own finger palette");
        Check(Apply(destination,inverse,request)==Result::Faulted,"partial finger write AV isolated");
        Check(std::memcmp(destination,original.data(),sizeof(original))==0,"all partially changed fingers roll back before native consumer");
        VirtualFree(memory,0,MEM_RELEASE);
    }
    std::printf("ODST fingers: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
