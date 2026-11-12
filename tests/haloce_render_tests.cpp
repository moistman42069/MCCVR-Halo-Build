#include "haloce_anniversary_logic.h"
#include <cstdio>
#include <cstring>
#include <limits>
#include <fstream>

using namespace halo_ce;
int main(int argc,char** argv)
{
    int failures=0;
    const auto check=[&](bool ok,const char* what) {
        if (!ok) { std::fprintf(stderr,"CE: %s\n",what); ++failures; }
    };
    const auto near=[](float a,float b) { return std::fabs(a-b)<0.00001f; };
    if (argc==4&&std::strcmp(argv[1],"--native-gameplay-bridge-fixture")==0)
    {
        std::ifstream source(argv[2],std::ios::binary);
        std::ofstream destination(argv[3],std::ios::binary);
        uint32_t count{};
        if (!source.read(reinterpret_cast<char*>(&count),sizeof(count))||!count||count>1000||
            !destination.write(reinterpret_cast<const char*>(&count),sizeof(count))) return 1;
        for (uint32_t index=0;index<count;++index)
        {
            SaberCamera saber{};Vec3 offset{};float bias{};Camera mapped{},native{};
            if (!source.read(reinterpret_cast<char*>(&saber),sizeof(saber))||
                !source.read(reinterpret_cast<char*>(&offset),sizeof(offset))||
                !source.read(reinterpret_cast<char*>(&bias),sizeof(bias))||
                !NativeCameraFromSaber(saber,mapped)||
                !RecoverNativeCameraFromSaberBridge(mapped,offset,bias,native)||
                !destination.write(reinterpret_cast<const char*>(&native),sizeof(native))) return 1;
        }
        return 0;
    }
    Camera stock{};
    stock.position={10,20,30}; stock.forward={1,0,0}; stock.up={0,0,1};
    stock.viewport=stock.window={0,0,1000,1000};
    stock.nearPlane=0.01f; stock.farPlane=1000; stock.verticalFov=1.2f;
    stock.nativeFlags=0x1234; stock.nativePlane[0]=42;
    Window window{}; window.player=0; window.render=window.raster=stock;
    check(ValidPrimary(window),"normal primary window admitted");
    window.isUi=1; check(!ValidPrimary(window),"UI window rejected");
    window.isUi=0; window.player=1; check(!ValidPrimary(window),"split-screen secondary rejected");
    window.player=0; ++window.raster.viewport.left;
    check(!ValidPrimary(window),"mismatched camera viewports rejected");
    Tracking tracking{}; tracking.serial=7; tracking.spaceEpoch=3; tracking.generation=2;
    tracking.eyes[0].offset={-0.032f,0,0}; tracking.eyes[1].offset={0.032f,0,0};
    for (Eye& eye:tracking.eyes) {
        eye.fov[0]=-0.85f; eye.fov[1]=0.95f; eye.fov[2]=0.8f; eye.fov[3]=-0.9f;
    }
    Reference reference{}; reference.spaceEpoch=3; reference.generation=2;
    Cover cover{};
    check(BuildCover(tracking,stock.viewport,cover),"asymmetric runtime FOV covered");
    check(near(cover.halfX,0.95f)&&near(cover.halfY,0.95f),"square raster covers widest eye edge");
    Camera left{},right{};
    check(BuildEye(stock,tracking,reference,0,0.33f,true,cover,left)&&
          BuildEye(stock,tracking,reference,1,0.33f,true,cover,right),"both eyes generated");
    check(near(left.position.y,20+0.032f*0.33f)&&
          near(right.position.y,20-0.032f*0.33f),"IPD follows CE forward/up basis");
    check(near(left.position.x,10)&&near(left.position.z,30),"IPD has no forward or vertical drift");
    check(near(left.forward.x,1)&&near(left.up.z,1),"identity tracking preserves native orientation");
    check(left.nativeFlags==stock.nativeFlags&&Same(left.viewport,stock.viewport)&&
          left.nearPlane==stock.nearPlane&&left.farPlane==stock.farPlane&&
          left.nativePlane[0]==42,"non-owned camera fields preserved");
    tracking.headPosition={0,1,-2};
    check(BuildEye(stock,tracking,reference,0,0.33f,true,cover,left)&&
          near(left.position.x,10.66f)&&near(left.position.z,30.33f),"metres translate in native axes");
    check(BuildEye(stock,tracking,reference,0,0.33f,false,cover,left)&&
          near(left.position.x,10)&&near(left.position.z,30),"positional off preserves binocular separation only");
    tracking.headPosition={};
    const float s=std::sqrt(0.5f);
    tracking.headOrientation=tracking.eyes[0].orientation={0,s,0,s};
    check(BuildEye(stock,tracking,reference,0,0.33f,true,cover,left)&&
          near(left.forward.x,0)&&near(left.forward.y,1),"90-degree physical head turn has correct sign");
    reference.orientation=tracking.headOrientation;
    check(BuildEye(stock,tracking,reference,0,0.33f,true,cover,left)&&
          near(left.forward.x,1)&&near(left.forward.y,0),"recenter removes initial head rotation");
    const Camera saved=left;
    ++tracking.generation;
    check(!BuildEye(stock,tracking,reference,0,0.33f,true,cover,left)&&
          std::memcmp(&left,&saved,sizeof(left))==0,"previous title generation reference rejected");
    --tracking.generation;
    tracking.spaceEpoch=4;
    check(!BuildEye(stock,tracking,reference,0,0.33f,true,cover,left)&&
          std::memcmp(&left,&saved,sizeof(left))==0,"space reset rejects without partial output");
    tracking.spaceEpoch=3;
    tracking.eyes[0].offset.x=std::numeric_limits<float>::quiet_NaN();
    check(!BuildEye(stock,tracking,reference,0,0.33f,true,cover,left),"NaN eye rejected");
    tracking.eyes[0].offset={0.6f,0,0};
    check(!BuildEye(stock,tracking,reference,0,0.33f,true,cover,left),"implausible eye offset rejected");
    check(!BuildEye(stock,tracking,reference,2,0.33f,true,cover,left),"invalid eye index rejected");
    check(!ExactPair(0,0,0)&&!ExactPair(7,7,6)&&ExactPair(7,7,7),"only complete current serial pair admitted");
    SaberPose saber{};
    check(BuildSaberPose(stock,{100,200,300},0.25f,saber),"verified CE to Saber bridge pose");
    check(near(saber.matrix[0],0)&&near(saber.matrix[1],0)&&near(saber.matrix[2],1)&&
          near(saber.matrix[4],0)&&near(saber.matrix[5],1)&&near(saber.matrix[6],0)&&
          near(saber.matrix[8],1)&&near(saber.matrix[9],0)&&near(saber.matrix[10],0),
          "Saber axes match CE bridge cross product and signed axis permutation");
    check(near(saber.matrix[12],130.73f)&&near(saber.matrix[13],291.44f)&&
          near(saber.matrix[14],239.04f)&&saber.matrix[15]==1,
          "bridge scale, world offset and forward bias applied once");
    const SaberPose savedSaber=saber;
    check(!BuildSaberPose(stock,{0,std::numeric_limits<float>::infinity(),0},0,saber)&&
          std::memcmp(&saber,&savedSaber,sizeof(saber))==0,
          "invalid bridge origin rejected without partial pose");
    SaberCamera saberStock{};
    BuildSaberPose(stock,{},0,saberStock.pose);
    saberStock.viewportWidth=saberStock.viewportHeight=1000;
    saberStock.nearPlane=stock.nearPlane*kSaberUnitsPerNativeUnit;
    saberStock.farPlane=stock.farPlane*kSaberUnitsPerNativeUnit;
    saberStock.verticalFovDegrees=stock.verticalFov*57.29577951308232f;
    saberStock.derived160[100]=0xab;
    Camera roundTrip{};
    check(NativeCameraFromSaber(saberStock,roundTrip)&&
          near(roundTrip.position.x,stock.position.x)&&
          near(roundTrip.position.y,stock.position.y)&&
          near(roundTrip.position.z,stock.position.z),"Saber camera round trip preserves position");
    for (Vec3 offset:{Vec3{},Vec3{100,200,300}})
        for (float bias:{0.0f,.25f,-.5f})
        {
            SaberCamera shifted=saberStock;
            check(BuildSaberPose(stock,offset,bias,shifted.pose),"native bridge fixture builds");
            Camera mapped{},recovered{};
            check(NativeCameraFromSaber(shifted,mapped)&&
                RecoverNativeCameraFromSaberBridge(mapped,offset,bias,recovered)&&
                near(recovered.position.x,stock.position.x)&&
                near(recovered.position.y,stock.position.y)&&
                near(recovered.position.z,stock.position.z),"gameplay recovery removes native bridge offset and bias exactly once");
            mapped.position=recovered.position;
            check(std::memcmp(&mapped,&recovered,sizeof(mapped))==0,"gameplay recovery changes only position");
        }
    Camera invalidRecovery=stock;
    check(!RecoverNativeCameraFromSaberBridge(stock,{0,0,std::numeric_limits<float>::infinity()},0,invalidRecovery)&&
        std::memcmp(&stock,&invalidRecovery,sizeof(stock))==0,"invalid bridge offset preserves output");
    tracking.headOrientation={}; tracking.eyes[0].orientation={};
    tracking.eyes[0].offset={-0.032f,0,0}; reference.orientation={};
    SaberCamera saberEye{};
    check(StageSaberEye(saberStock,tracking,reference,0,0.33f,true,saberEye,cover)&&
          saberEye.derived160[100]==0xab&&saberEye.nearPlane==saberStock.nearPlane&&
          saberEye.farPlane==saberStock.farPlane,
          "staged Anniversary eye preserves every non-owned field");
    check(near(saberEye.pose.matrix[14],-20*3.048f-0.032f*0.33f*3.048f),
          "Saber binocular translation uses independently verified scale and axes");
    {
        // H3 parity: recenter owns yaw/position; physical pitch and roll remain
        // absolute against world up, independent of the stock look pitch.
        const auto nearVector=[](Vec3 a,Vec3 b) { return Dot(a-b,a-b)<0.000001f; };
        const Quat headYaw{0,std::sin(.35f),0,std::cos(.35f)};
        for (float nativePitch:{-.8f,0.0f,.6f})
            for (float referencePitch:{-.55f,0.0f,.45f})
                for (float referenceRoll:{-.3f,0.0f,.4f})
                {
                    Camera source=stock;
                    source.forward={std::cos(nativePitch),0,std::sin(nativePitch)};
                    source.up={-std::sin(nativePitch),0,std::cos(nativePitch)};
                    Tracking sample=tracking;
                    Reference origin=reference;
                    origin.orientation=Multiply(headYaw,Multiply(
                        {std::sin(referencePitch/2),0,0,std::cos(referencePitch/2)},
                        {0,0,std::sin(referenceRoll/2),std::cos(referenceRoll/2)}));
                    sample.headOrientation=headYaw;
                    sample.headPosition=origin.position+Vec3{0,.25f,0};
                    for (int eye=0;eye<2;++eye)
                    {
                        sample.eyes[eye].orientation=headYaw;
                        sample.eyes[eye].offset=Rotate(headYaw,{eye?.032f:-.032f,0,0});
                        Camera classic{};
                        check(BuildEye(source,sample,origin,eye,.33f,true,cover,classic)&&
                            nearVector(classic.forward,{1,0,0})&&nearVector(classic.up,{0,0,1})&&
                            near(classic.position.z,source.position.z+.25f*.33f)&&
                            near(classic.position.x,source.position.x),
                            "tilted recenter and native pitch cannot tilt the level world or physical height");
                        SaberCamera nativeSaber=saberStock,anniversary{};
                        Camera anniversaryCamera{};Cover anniversaryCover{};
                        check(BuildSaberPose(source,{},0,nativeSaber.pose)&&
                            StageSaberEye(nativeSaber,sample,origin,eye,.33f,true,anniversary,anniversaryCover)&&
                            NativeCameraFromSaber(anniversary,anniversaryCamera)&&
                            nearVector(anniversaryCamera.forward,classic.forward)&&
                            nearVector(anniversaryCamera.up,classic.up)&&
                            nearVector(anniversaryCamera.position,classic.position),
                            "Original and Anniversary use the same level recenter frame in both eyes");
                    }
                    // Recenter while still tilted must retain physical tilt,
                    // rather than baking the tilted pose into a neutral room.
                    sample.headOrientation=origin.orientation;
                    sample.eyes[0].orientation=origin.orientation;
                    Camera physical{};
                    const Quat tilt=Multiply(
                        {std::sin(referencePitch/2),0,0,std::cos(referencePitch/2)},
                        {0,0,std::sin(referenceRoll/2),std::cos(referenceRoll/2)});
                    check(BuildEye(source,sample,origin,0,.33f,false,cover,physical)&&
                        nearVector(physical.forward,ToNative(stock,Rotate(tilt,{0,0,-1})))&&
                        nearVector(physical.up,ToNative(stock,Rotate(tilt,{0,1,0}))),
                        "recenter preserves current physical pitch and roll instead of zeroing them");
                }
    }
    {
        // The same horizontal frame must also preserve binocular separation
        // through native bank, near-vertical look, and the exact pitch poles.
        constexpr float halfPi=1.5707963267948966f;
        const auto nearVector=[](Vec3 a,Vec3 b) { return Dot(a-b,a-b)<0.000001f; };
        for (float nativeYaw:{-2.4f,.7f})
            for (float nativePitch:{-halfPi,-halfPi+.00001f,-.8f,.6f,halfPi-.00001f,halfPi})
                for (float headPitch:{-halfPi,-halfPi+.001f,-.6f,.5f,halfPi-.001f,halfPi})
                {
                    Camera source=stock;
                    const float cy=std::cos(nativeYaw),sy=std::sin(nativeYaw);
                    const float cp=std::cos(nativePitch),sp=std::sin(nativePitch);
                    source.forward={cp*cy,cp*sy,sp};source.up={-sp*cy,-sp*sy,cp};
                    Tracking sample=tracking;Reference origin=reference;
                    const Quat yaw{0,std::sin(-.4f),0,std::cos(-.4f)};
                    origin.orientation=Multiply(yaw,{std::sin(.25f),0,0,std::cos(.25f)});
                    sample.headOrientation=Multiply(yaw,Multiply(
                        {std::sin(headPitch/2),0,0,std::cos(headPitch/2)},
                        {0,0,std::sin(.2f),std::cos(.2f)}));
                    sample.headPosition=origin.position;
                    Camera eyes[2]{};
                    for (int eye=0;eye<2;++eye)
                    {
                        sample.eyes[eye].orientation=sample.headOrientation;
                        sample.eyes[eye].offset=Rotate(sample.headOrientation,{eye?.032f:-.032f,0,0});
                        check(BuildEye(source,sample,origin,eye,.33f,true,cover,eyes[eye]),
                            "physical and native pitch poles retain a finite tracked eye");
                    }
                    const Vec3 separation=eyes[1].position-eyes[0].position;
                    check(nearVector((eyes[0].position+eyes[1].position)*.5f,source.position)&&
                        std::fabs(std::sqrt(Dot(separation,separation))-.064f*.33f)<.00001f&&
                        std::fabs(Dot(separation,eyes[0].forward))<.00001f&&
                        std::fabs(Dot(separation,eyes[0].up))<.00001f&&
                        nearVector(eyes[0].forward,eyes[1].forward)&&nearVector(eyes[0].up,eyes[1].up),
                        "rolled IPD stays symmetric and perpendicular to physical eye orientation at all pitches");
                    if (std::fabs(cp)>.01f)
                    {
                        // Authored bank cannot bank the room or its displacement.
                        source.up=Rotate({source.forward.x*std::sin(.3f),
                            source.forward.y*std::sin(.3f),source.forward.z*std::sin(.3f),std::cos(.3f)},source.up);
                        Camera banked{};
                        check(BuildEye(source,sample,origin,0,.33f,true,cover,banked)&&
                            nearVector(banked.position,eyes[0].position)&&
                            nearVector(banked.forward,eyes[0].forward)&&nearVector(banked.up,eyes[0].up),
                            "native bank does not alter world-locked eye pose");
                    }
                    // At exact vertical HMD reference there is no forward yaw.
                    // Its right-axis fallback must remain finite and q/-q invariant.
                    origin.orientation=sample.headOrientation;
                    Camera pole{};Quat inverse{};
                    check(BuildTrackingFrame(source,origin,pole,inverse)&&Valid(pole)&&Valid(inverse),
                        "recenter at physical pitch pole has a finite horizontal fallback");
                    origin.orientation={-origin.orientation.x,-origin.orientation.y,
                        -origin.orientation.z,-origin.orientation.w};
                    Camera negative{};Quat negativeInverse{};
                    check(BuildTrackingFrame(source,origin,negative,negativeInverse)&&
                        nearVector(pole.forward,negative.forward)&&
                        nearVector(Rotate(inverse,{0,0,-1}),Rotate(negativeInverse,{0,0,-1})),
                        "equivalent quaternion signs cannot change recentered heading");
                }
        Camera sentinel=stock;Quat inverse{.1f,.2f,.3f,.4f};
        const Quat savedInverse=inverse;Reference invalid=reference;
        invalid.orientation.w=std::numeric_limits<float>::quiet_NaN();
        check(!BuildTrackingFrame(stock,invalid,sentinel,inverse)&&
            std::memcmp(&sentinel,&stock,sizeof(stock))==0&&
            std::memcmp(&inverse,&savedInverse,sizeof(inverse))==0,
            "invalid tracking frame leaves both outputs untouched");
        // THE SEAT'S VIEW HEADING (Reference::seatHeading). In a seat the native camera may follow the very aim the
        // hand writes (a Warthog gunner's camera is the gun), so the frame's heading is the player's control yaw.
        {
            const auto cameraLooking=[&](float yaw,float pitch)
            {
                Camera camera=stock;
                camera.forward={std::cos(pitch)*std::cos(yaw),std::cos(pitch)*std::sin(yaw),std::sin(pitch)};
                const Vec3 right{std::sin(yaw),-std::cos(yaw),0};
                camera.up=Cross(right,camera.forward);
                return camera;
            };
            Reference seat=reference;
            check(!seat.seatHeadingValid,"a reference has no seat heading until one is read");
            Camera onFoot{};Quat onFootInverse{};
            const Camera looking=cameraLooking(0.7f,-0.4f);
            check(Valid(looking)&&BuildTrackingFrame(looking,seat,onFoot,onFootInverse)&&
                nearVector(onFoot.forward,{std::cos(0.7f),std::sin(0.7f),0})&&nearVector(onFoot.up,{0,0,1}),
                "on foot the frame keeps the native camera's heading");
            seat.seatHeadingValid=true;seat.seatHeading={0,1,0};
            bool fixed=true;
            for (float yaw=-3.0f;yaw<3.1f;yaw+=0.37f)
                for (float pitch=-1.2f;pitch<1.3f;pitch+=0.6f)
                {
                    Camera frame{};Quat inverse{};
                    const Camera gun=cameraLooking(yaw,pitch);
                    fixed=fixed&&Valid(gun)&&BuildTrackingFrame(gun,seat,frame,inverse)&&
                        nearVector(frame.forward,{0,1,0})&&nearVector(frame.up,{0,0,1})&&
                        nearVector(frame.position,gun.position)&&
                        nearVector(Rotate(inverse,{0,0,-1}),Rotate(onFootInverse,{0,0,-1}));
                }
            check(fixed,"in a seat the frame's heading is the control yaw wherever the native camera points; "
                "its position and the recentre yaw are unchanged");
            // Not a heading: the native camera's, as before (never a rejected frame).
            Camera fallback{};Quat fallbackInverse{};
            seat.seatHeading={0,0,1};
            check(BuildTrackingFrame(looking,seat,fallback,fallbackInverse)&&nearVector(fallback.forward,onFoot.forward),
                "a vertical seat heading falls back to the native camera's");
            seat.seatHeading={std::numeric_limits<float>::quiet_NaN(),1,0};
            check(BuildTrackingFrame(looking,seat,fallback,fallbackInverse)&&nearVector(fallback.forward,onFoot.forward),
                "a non-finite seat heading falls back to the native camera's");
            // The runaway this exists for. The camera follows the aim; the hand is held 10 degrees off the
            // reference's forward. Through the camera's own frame every update adds the offset again (a turn rate);
            // through the seat heading the aim is one fixed direction.
            const float offset=10.0f*0.017453292f;
            const Quat hand=Multiply({0,std::sin(offset*.5f),0,std::cos(offset*.5f)},reference.orientation);
            const auto aimThrough=[&](const Reference& via,float cameraYaw)
            {
                Camera frame{};Quat inverse{};
                if (!BuildTrackingFrame(cameraLooking(cameraYaw,-0.2f),via,frame,inverse)) return 1000.0f;
                const Vec3 aim=ToNative(frame,Rotate(Multiply(inverse,hand),{0,0,-1}));
                return std::atan2(aim.y,aim.x);
            };
            Reference control=reference;
            control.seatHeadingValid=true;control.seatHeading={std::cos(0.3f),std::sin(0.3f),0};
            float followed=0.3f,held=0.3f,travelled=0,heldSpread=0;
            const float heldFirst=aimThrough(control,held);
            for (int update=0;update<30;++update)
            {
                const float next=aimThrough(reference,followed);
                travelled+=std::fabs(std::remainder(next-followed,6.2831853f));
                followed=next;                                   // the camera follows the aim it was given
                held=aimThrough(control,held);
                heldSpread=std::max(heldSpread,std::fabs(std::remainder(held-heldFirst,6.2831853f)));
            }
            check(std::fabs(travelled-30*offset)<0.001f,
                "CONTROL: through an aim-following camera a 10 degree hand offset turns 10 degrees every update");
            check(heldSpread<0.00001f&&std::fabs(std::fabs(std::remainder(heldFirst-0.3f,6.2831853f))-offset)<0.0001f,
                "through the seat heading the same hand aims one fixed direction, 10 degrees off the control yaw");
        }
    }
    const SaberCamera savedEye=saberEye;
    saberStock.viewportWidth=std::numeric_limits<float>::infinity();
    check(!StageSaberEye(saberStock,tracking,reference,0,0.33f,true,saberEye,cover)&&
          std::memcmp(&saberEye,&savedEye,sizeof(saberEye))==0,
          "invalid raster rejected before float-to-integer conversion or partial writes");
    return failures?1:0;
}
