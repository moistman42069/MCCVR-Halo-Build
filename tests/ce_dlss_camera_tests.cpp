#include <cstdio>
#include <cstdlib>
#include <fstream>
#include "../src/common/haloce_dlss_logic.h"
static unsigned checks;
static void Check(bool pass,const char* why){++checks;if(!pass){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
int main(int argc,char** argv) {
    if(argc==3&&std::string(argv[1])=="--native") {
        halo_ce::SaberCamera camera{};float origin[4]{},packed[16]{};
        std::ifstream input(argv[2],std::ios::binary);
        Check(bool(input.read(reinterpret_cast<char*>(&camera),sizeof(camera))&&
            input.read(reinterpret_cast<char*>(origin),sizeof(origin))&&
            input.read(reinterpret_cast<char*>(packed),sizeof(packed))),"native fixture readable");
        const auto sample=halo_ce::DlssSaberCamera(camera,origin,packed);
        Check(sample.valid,"actual native camera constants decode");
        const auto* pose=camera.pose.matrix;
        for(const float distance:{1.f,4.f,80.f}) for(const float side:{-.3f,0.f,.4f}) {
            float relative[3]{};
            for(int axis=0;axis<3;++axis)
                relative[axis]=pose[axis]*side+pose[4+axis]*.17f+pose[8+axis]*distance;
            float clip[4]{};
            for(int column=0;column<4;++column) {
                clip[column]=packed[column*4+3];
                for(int axis=0;axis<3;++axis)clip[column]+=relative[axis]*packed[column*4+axis];
            }
            Check(clip[3]>0,"native forward point has positive clip W");
            const auto rel=halo_ce::FromSaber({relative[0],relative[1],relative[2]})*(1.f/halo_ce::kSaberUnitsPerNativeUnit);
            const float nativeRelative[]{rel.x,rel.y,rel.z};
            const float t=dlss::Dot(nativeRelative,sample.forward);
            const float x=dlss::Dot(nativeRelative,sample.right)/(t*sample.projection.tanX)+sample.projection.centerX;
            const float y=dlss::Dot(nativeRelative,sample.up)/(t*sample.projection.tanY)+sample.projection.centerY;
            const float z=sample.projection.depthA+sample.projection.depthB/t;
            Check(std::abs(x-clip[0]/clip[3])<.0003f&&std::abs(y-clip[1]/clip[3])<.0003f&&
                std::abs(z-clip[2]/clip[3])<.0003f,"decoded DLSS projection matches actual native constants");
        }
        origin[0]+=1;Check(!halo_ce::DlssSaberCamera(camera,origin,packed).valid,"foreign camera origin rejected");
    } else {
        halo_ce::Camera camera{};camera.forward={1,0,0};camera.up={0,0,1};camera.verticalFov=1;
        camera.viewport=camera.window={0,0,800,1200};camera.nearPlane=.03f;camera.farPlane=100;
        const float n=camera.nearPlane,f=camera.farPlane;
        float projection[16]{1.5f,0,0,0,0,2.f,0,0,0,0,n/(f-n),-1,0,0,f*n/(f-n),0};
        const auto sample=halo_ce::DlssClassicCamera(camera,projection);
        Check(sample.valid&&sample.projection.depthInverted,"CE Classic own reversed depth");
        Check(std::abs(dlss::ViewDistance(sample.projection,1)-n)<.00001f,"near plane depth one");
        Check(std::abs(dlss::ViewDistance(sample.projection,0)-f)<.001f,"far plane depth zero");
        projection[2]=.5f;Check(!halo_ce::DlssClassicCamera(camera,projection).valid,"oblique/native special lens stays ordinary");
    }
    std::printf("PASS: %u CE DLSS camera checks\n",checks);
}
