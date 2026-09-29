#include "../src/common/haloce_first_person_logic.h"
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "CE first-person check failed at line %d: %s\n", __LINE__, #condition); return 1; } } while (false)
#include <cstdio>
#include <limits>
#include <fstream>
using namespace halo_ce;
namespace
{
bool Near(Vec3 a,Vec3 b) { return Dot(a-b,a-b)<0.000001f; }
void Name(AnimationNode& node,const char* name,int parent)
{ std::strcpy(node.name,name);node.parent=int16_t(parent); }
}
int main(int argc,char** argv)
{
    if (argc==4&&std::strcmp(argv[1],"--particle-constants")==0)
    {
        std::ifstream input(argv[2],std::ios::binary);float constants[201*4]{};
        CHECK(input.read(reinterpret_cast<char*>(constants),sizeof(constants)));
        unsigned changed{};CHECK(SelectSaberTrackedParticleProjection(constants,201,changed));
        std::ofstream output(argv[3],std::ios::binary);
        CHECK(output.write(reinterpret_cast<const char*>(constants),sizeof(constants)));
        return 0;
    }
    {
        std::array<float,201*4> constants{};constants.fill(7);
        for (size_t emitter=0;emitter<9;++emitter) constants[(21+emitter*20+10)*4]=emitter%2?0.0f:1.0f;
        const auto before=constants;unsigned changed{};
        CHECK(SelectSaberTrackedParticleProjection(constants.data(),201,changed)&&changed==5);
        for (size_t index=0;index<constants.size();++index)
        {
            bool selector=false;
            for (size_t emitter=0;emitter<9;++emitter) selector|=index==(21+emitter*20+10)*4;
            CHECK(constants[index]==(selector?0:before[index]));
        }
        for (float invalid:{.5f,-1.0f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()})
        {
            constants=before;constants[(21+8*20+10)*4]=invalid;const auto malformed=constants;
            CHECK(!SelectSaberTrackedParticleProjection(constants.data(),201,changed)&&changed==0);
            CHECK(!std::memcmp(constants.data(),malformed.data(),sizeof(constants)));
        }
        constants=before;
        CHECK(!SelectSaberTrackedParticleProjection(constants.data(),200,changed));
        CHECK(constants==before&&!SelectSaberTrackedParticleProjection(nullptr,201,changed));
    }
    if (argc==4&&std::strcmp(argv[1],"--floating-mesh-fixture")==0)
    {
        std::ifstream input(argv[2],std::ios::binary);
        uint32_t count{},flags{};float primaryScale{},supportScale{};
        AnimationNode nodes[kFirstPersonMaxNodes]{};
        NodeMatrix authored[kFirstPersonMaxNodes]{};
        CHECK(input.read(reinterpret_cast<char*>(&count),sizeof(count))&&count&&count<=kFirstPersonMaxNodes);
        CHECK(input.read(reinterpret_cast<char*>(nodes),count*sizeof(AnimationNode)));
        CHECK(input.read(reinterpret_cast<char*>(authored),count*sizeof(NodeMatrix)));
        CHECK(input.read(reinterpret_cast<char*>(&flags),sizeof(flags)));
        CHECK(input.read(reinterpret_cast<char*>(&primaryScale),sizeof(primaryScale)));
        CHECK(input.read(reinterpret_cast<char*>(&supportScale),sizeof(supportScale)));
        FirstPersonBinding binding{};
        CHECK(BuildFirstPersonBinding(25,3,nodes,count,binding));
        Camera camera{};camera.position={100,200,300};camera.forward={1,0,0};camera.up={0,0,1};
        camera.verticalFov=1;camera.viewport={0,0,100,100};camera.window=camera.viewport;
        camera.nearPlane=.01f;camera.farPlane=100;
        Tracking tracking{};tracking.serial=4;tracking.generation=3;tracking.spaceEpoch=2;
        Reference reference{};reference.generation=3;reference.spaceEpoch=2;
        auto& rig=tracking.controllers;
        rig.controlsPresentationBlocked=false;rig.primaryAim.valid=rig.support.valid=true;
        rig.primaryAim.position={.3f,-.2f,-.6f};rig.support.position={-.3f,-.1f,-.4f};
        rig.gunScale=primaryScale;rig.supportScale=supportScale;
        rig.gunForwardM=rig.supportForwardM=0;rig.primaryShoulderDrop=0;
        rig.floatingHands=(flags&1)!=0;rig.leftHanded=(flags&2)!=0;
        rig.handAlignment=(flags&4)!=0;rig.twoHandAimActive=(flags&8)!=0;
        std::array<NodeMatrix,kFirstPersonMaxNodes> palette{};
        CHECK(BuildTrackedFirstPersonPalette(binding,authored,camera,tracking,reference,1,true,palette));
        std::ofstream output(argv[3],std::ios::binary);
        CHECK(output.write(reinterpret_cast<const char*>(palette.data()),count*sizeof(NodeMatrix)));
        return 0;
    }
    if (argc==4&&std::strcmp(argv[1],"--classic-native-projection-fixture")==0)
    {
        std::ifstream input(argv[2],std::ios::binary);
        uint64_t returnRva{};float fov{};
        CHECK(input.read(reinterpret_cast<char*>(&returnRva),sizeof(returnRva)));
        CHECK(input.read(reinterpret_cast<char*>(&fov),sizeof(fov)));
        if (IsClassicFirstPersonLensCallsite(uintptr_t(returnRva))) (void)SelectClassicTrackedProjection(fov);
        std::ofstream output(argv[3],std::ios::binary);
        CHECK(output.write(reinterpret_cast<const char*>(&fov),sizeof(fov)));
        return 0;
    }
    for (float initial:{.9671381116f,1.2f,2.3f})
    {
        float fov=initial;CHECK(SelectClassicTrackedProjection(fov));CHECK(fov==-2);
    }
    for (float invalid:{-2.0f,-1.0f,0.0f,3.2f,std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity()})
    {
        float fov=invalid;CHECK(!SelectClassicTrackedProjection(fov));
        CHECK(std::memcmp(&fov,&invalid,sizeof(fov))==0);
    }
    for (uintptr_t caller:{0xc1769fu,0xc159b1u,0xbf4440u,0xc12354u,0xc12b52u})
    {
        CHECK(IsClassicFirstPersonLensCallsite(caller));
        CHECK(!IsClassicFirstPersonLensCallsite(caller-1));
        CHECK(!IsClassicFirstPersonLensCallsite(caller+1));
    }
    CHECK(!IsClassicFirstPersonLensCallsite(0xc196b6)); // native restore
    if (argc==4&&(std::strcmp(argv[1],"--saber-native-projection-fixture")==0||
        std::strcmp(argv[1],"--saber-native-projection-layout-fixture")==0))
    {
        std::ifstream input(argv[2],std::ios::binary);
        uint32_t flags{},offset=0x170;float constants[96]{};
        CHECK(input.read(reinterpret_cast<char*>(&flags),sizeof(flags)));
        if (std::strcmp(argv[1],"--saber-native-projection-layout-fixture")==0)
            CHECK(input.read(reinterpret_cast<char*>(&offset),sizeof(offset)));
        CHECK(offset==0x170||offset==0x20||offset==0x70);
        CHECK(input.read(reinterpret_cast<char*>(constants),sizeof(constants)));
        float selector[4]{};std::memcpy(selector,constants+offset/4,sizeof(selector));
        if (SelectSaberTrackedProjection(flags,selector))
            std::memcpy(constants+offset/4,selector,sizeof(selector));
        std::ofstream output(argv[3],std::ios::binary);
        CHECK(output.write(reinterpret_cast<const char*>(constants),sizeof(constants)));
        return 0;
    }
    for (uint32_t flags:{0u,1u,0x10000000u,0x90000001u})
    {
        float selector[4]{1,1,1,1};
        const bool firstPerson=(flags&0x10000000u)!=0;
        CHECK(SelectSaberTrackedProjection(flags,selector)==firstPerson);
        for (float value:selector) CHECK(value==(firstPerson?0.0f:1.0f));
    }
    for (float invalid:{0.0f,0.5f,-1.0f,std::numeric_limits<float>::quiet_NaN()})
    {
        float selector[4]{1,1,invalid,1},before[4]{};
        std::memcpy(before,selector,sizeof(before));
        CHECK(!SelectSaberTrackedProjection(0x10000000u,selector));
        CHECK(std::memcmp(before,selector,sizeof(before))==0);
    }
    if (argc==4&&std::strcmp(argv[1],"--saber-native-scale-fixture")==0)
    {
        std::ifstream input(argv[2],std::ios::binary);
        float scale{};SaberBoneMatrix converted{};
        CHECK(input.read(reinterpret_cast<char*>(&scale),sizeof(scale)));
        CHECK(input.read(reinterpret_cast<char*>(&converted),sizeof(converted)));
        CHECK(ApplySaberFirstPersonScale(scale,converted));
        std::ofstream output(argv[3],std::ios::binary);
        CHECK(output.write(reinterpret_cast<const char*>(&converted),sizeof(converted)));
        return 0;
    }
    // The Anniversary converter keeps native scale separate from its output
    // basis. Exercise the actual post-conversion adapter, including floating
    // arms, translations in Saber units, and all-or-nothing invalid input.
    const SaberBoneMatrix converted{{0,1,0,0, 0,0,-1,0, 1,0,0,0, 125,-340,78,1}};
    for (float scale:{0.00001f,0.3f,1.0f,3.0f})
    {
        SaberBoneMatrix scaled=converted;
        CHECK(ApplySaberFirstPersonScale(scale,scaled));
        for (size_t row=0;row<4;++row)
            for (size_t column=0;column<4;++column)
                CHECK(scaled.value[row*4+column]==converted.value[row*4+column]*
                    ((row<3&&column<3)?scale:1.0f));
    }
    for (float scale:{0.0f,-1.0f,100.0f,std::numeric_limits<float>::quiet_NaN()})
    {
        SaberBoneMatrix unchanged=converted;
        CHECK(!ApplySaberFirstPersonScale(scale,unchanged));
        CHECK(std::memcmp(&unchanged,&converted,sizeof(converted))==0);
    }
    SaberBoneMatrix malformed=converted;
    malformed.value[15]=0;
    const auto malformedBefore=malformed;
    CHECK(!ApplySaberFirstPersonScale(2.0f,malformed));
    CHECK(std::memcmp(&malformed,&malformedBefore,sizeof(malformed))==0);
    for (int argument=1;argument<argc;++argument)
    {
        std::ifstream fixture(argv[argument],std::ios::binary);
        uint32_t count{};AnimationNode official[kFirstPersonMaxNodes]{};
        CHECK(fixture.read(reinterpret_cast<char*>(&count),sizeof(count))&&count&&count<=kFirstPersonMaxNodes);
        CHECK(fixture.read(reinterpret_cast<char*>(official),count*sizeof(AnimationNode)));
        FirstPersonBinding actual{};
        CHECK(BuildFirstPersonBinding(25,3,official,count,actual));
        for (int side=0;side<2;++side) CHECK(actual.shoulder[side]>=0&&actual.elbow[side]>=0);
        // Every hidden official arm node has a proved left/right shoulder
        // ancestor. The graph root is the only remaining non-hand node.
        CHECK((actual.armMask[0]|actual.armMask[1]|actual.leftMask|actual.rightMask|1)==
            (count==64?~uint64_t{}:(uint64_t{1}<<count)-1));
    }
    AnimationNode nodes[9]{};
    Name(nodes[0],"frame bone24",0);
    Name(nodes[1],"frame r upperarm",0);
    Name(nodes[2],"frame l upperarm",0);
    Name(nodes[3],"frame r wriste",1);
    Name(nodes[4],"frame l wriste",2);
    Name(nodes[5],"frame gun",3);
    Name(nodes[6],"frame r index low",3);
    Name(nodes[7],"frame l index low",4);
    Name(nodes[8],"frame magazine",5);
    FirstPersonBinding binding{};
    CHECK(BuildFirstPersonBinding(25,3,nodes,9,binding));
    CHECK(binding.rightMask==((1ull<<3)|(1ull<<5)|(1ull<<6)|(1ull<<8)));
    CHECK(binding.leftMask==((1ull<<4)|(1ull<<7)));
    CHECK(binding.gunMask==((1ull<<5)|(1ull<<8)));
    NodeMatrix source[9]{};
    source[3].position={1,0,0};source[4].position={1,1,0};
    source[5].position={1.5f,0,0};source[6].position={1.1f,0,0};
    source[7].position={1.1f,1,0};source[8].position={1.5f,0,-0.1f};
    // The hand model has an authored quarter-turn relative to the gun.
    source[3].forward={0,1,0};source[3].left={-1,0,0};
    NodeMatrix primaryAim{},supportAim{};
    primaryAim.position={2,3,4};primaryAim.forward={0,1,0};primaryAim.left={-1,0,0};
    supportAim.position={-2,-3,-4};
    NodeMatrix primary{},support{};
    CHECK(BuildGripCarriers(binding,source,9,primaryAim,supportAim,primary,support));
    std::array<NodeMatrix,kFirstPersonMaxNodes> palette{};
    CHECK(RelocateFirstPersonPalette(binding,25,3,source,9,primary,support,false,palette));
    CHECK(Near(palette[3].position,primaryAim.position));
    CHECK(Near(palette[4].position,supportAim.position));
    CHECK(Near(palette[5].forward,primaryAim.forward));
    CHECK(Near(palette[5].position,{2,3.5f,4}));
    CHECK(Near(palette[8].position-palette[5].position,{0,0,-0.1f}));
    CHECK(Near(palette[7].position,{-1.9f,-3,-4}));
    CHECK(Near(palette[0].position,source[0].position));
    const auto independent=palette;
    CHECK(RelocateFirstPersonPalette(binding,25,3,source,9,primary,support,true,palette));
    CHECK(Near(palette[4].position,{1,3,4}));
    CHECK(!Near(palette[4].position,independent[4].position));
    const auto sentinel=palette;
    CHECK(!RelocateFirstPersonPalette(binding,26,3,source,9,primary,support,false,palette));
    CHECK(!RelocateFirstPersonPalette(binding,25,4,source,9,primary,support,false,palette));
    source[8].position.x=std::numeric_limits<float>::quiet_NaN();
    CHECK(!RelocateFirstPersonPalette(binding,25,3,source,9,primary,support,false,palette));
    CHECK(std::memcmp(&palette,&sentinel,sizeof(palette))==0);
    const auto saved=binding;
    nodes[2].parent=4;
    CHECK(!BuildFirstPersonBinding(25,3,nodes,9,binding));
    CHECK(std::memcmp(&binding,&saved,sizeof(binding))==0);
    nodes[2].parent=0;nodes[5].parent=4;
    CHECK(!BuildFirstPersonBinding(25,3,nodes,9,binding));

    Camera camera{};camera.forward={1,0,0};camera.up={0,0,1};
    camera.verticalFov=1;camera.viewport={0,0,100,100};camera.window=camera.viewport;
    camera.nearPlane=0.01f;camera.farPlane=100;
    Tracking tracking{};tracking.serial=4;tracking.generation=3;tracking.spaceEpoch=2;
    tracking.headPosition={0,0,-0.4f};
    Reference reference{};reference.generation=3;reference.spaceEpoch=2;
    ControllerPose controller{};controller.valid=true;controller.position={0.2f,0,-0.6f};
    NodeMatrix target{};
    CHECK(BuildControllerMatrix(camera,tracking,reference,controller,1,true,target));
    CHECK(Near(target.position,{0.6f,-0.2f,0}));
    CHECK(BuildControllerMatrix(camera,tracking,reference,controller,1,false,target));
    CHECK(Near(target.position,{0.2f,-0.2f,0}));
    controller.valid=false;
    CHECK(!BuildControllerMatrix(camera,tracking,reference,controller,1,true,target));
    {
        // BuildControllerMatrix also supplies the native controller shot ray.
        // Tilted recenter/native look must keep rendered hands, eye poses and
        // that ray in one world-level frame, for either positional setting.
        const Quat yaw{0,std::sin(.35f),0,std::cos(.35f)};
        const Quat pitch{std::sin(.2f),0,0,std::cos(.2f)};
        for (float nativePitch:{-1.5707963268f,-.8f,0.0f,.6f,1.5707963268f})
            for (float referencePitch:{-.6f,0.0f,.5f})
                for (float referenceRoll:{-.4f,0.0f,.5f})
                {
                    Camera source=camera;
                    source.position={3,4,5};
                    source.forward={0,std::cos(nativePitch),std::sin(nativePitch)};
                    source.up={0,-std::sin(nativePitch),std::cos(nativePitch)};
                    auto origin=reference;auto sample=tracking;
                    origin.position={.6f,1.2f,-.2f};
                    origin.orientation=Multiply(yaw,Multiply(
                        {std::sin(referencePitch/2),0,0,std::cos(referencePitch/2)},
                        {0,0,std::sin(referenceRoll/2),std::cos(referenceRoll/2)}));
                    sample.headPosition=origin.position+Rotate(yaw,{.1f,.1f,-.2f});
                    ControllerPose hand{};hand.valid=true;hand.orientation=Multiply(yaw,pitch);
                    hand.position=origin.position+Rotate(yaw,{.2f,.3f,-.6f});
                    NodeMatrix aim{};
                    CHECK(BuildControllerMatrix(source,sample,origin,hand,.33f,true,aim));
                    CHECK(Near(aim.position,source.position+Vec3{.2f,.6f,.3f}*.33f));
                    CHECK(Near(aim.forward,{0,std::cos(.4f),std::sin(.4f)}));
                    CHECK(Near(aim.up,{0,-std::sin(.4f),std::cos(.4f)}));
                    sample.headPosition=hand.position;
                    sample.headOrientation=sample.eyes[0].orientation=hand.orientation;
                    sample.eyes[0].offset={};
                    Camera eye{};Cover eyeCover{1,.5f,.5f};
                    CHECK(BuildEye(source,sample,origin,0,.33f,true,eyeCover,eye));
                    CHECK(Near(eye.position,aim.position)&&Near(eye.forward,aim.forward)&&Near(eye.up,aim.up));
                    sample.headPosition=origin.position+Rotate(yaw,{.1f,.1f,-.2f});
                    CHECK(BuildControllerMatrix(source,sample,origin,hand,.33f,false,aim));
                    CHECK(Near(aim.position,source.position+Vec3{.1f,.4f,.2f}*.33f));
                    CHECK(Near(aim.forward,{0,std::cos(.4f),std::sin(.4f)}));
                    const NodeMatrix unchanged=aim;
                    ++origin.spaceEpoch;
                    CHECK(!BuildControllerMatrix(source,sample,origin,hand,.33f,false,aim));
                    CHECK(std::memcmp(&aim,&unchanged,sizeof(aim))==0);
                }
    }

    // Exercise the production builder with a complete CE arm chain. The
    // hand target must stay exact outside reach; shoulders must stay planted.
    AnimationNode armNodes[11]{};
    Name(armNodes[0],"frame bone24",0);
    Name(armNodes[1],"frame l upperarm",0);Name(armNodes[2],"frame r upperarm",0);
    Name(armNodes[3],"frame l forearm",1);Name(armNodes[4],"frame r forearm",2);
    Name(armNodes[5],"frame l wriste",3);Name(armNodes[6],"frame r wriste",4);
    Name(armNodes[7],"frame body",6); // actual official Needler weapon root
    Name(armNodes[8],"frame l index low",5);Name(armNodes[9],"frame r index low",6);
    Name(armNodes[10],"frame needle01",7);
    CHECK(BuildFirstPersonBinding(53,3,armNodes,11,binding));
    NodeMatrix authored[11]{};
    authored[1].position={0,0.2f,-0.1f};authored[2].position={0,-0.2f,-0.1f};
    authored[3].position={0.2f,0.2f,-0.2f};authored[4].position={0.2f,-0.2f,-0.2f};
    authored[5].position={0.4f,0.2f,-0.1f};authored[6].position={0.4f,-0.2f,-0.1f};
    authored[7].position={0.5f,-0.2f,-0.1f};authored[8].position={0.45f,0.2f,-0.1f};
    authored[9].position={0.45f,-0.2f,-0.1f};authored[10].position={0.6f,-0.2f,-0.1f};
    auto& rig=tracking.controllers;
    rig.controlsPresentationBlocked=false;
    rig.primaryAim.valid=rig.support.valid=true;
    rig.primaryAim.position={0.3f,0,-0.5f};rig.support.position={-0.3f,0,-0.3f};
    rig.gunScale=rig.supportScale=1;rig.gunForwardM=rig.supportForwardM=0;
    rig.primaryShoulderDrop=0;rig.floatingHands=false;
    CHECK(BuildTrackedFirstPersonPalette(binding,authored,camera,tracking,reference,1,true,palette));
    CHECK(Near(palette[6].position,{0.5f,-0.3f,0}));
    CHECK(Near(palette[5].position,{0.3f,0.3f,0}));
    CHECK(Near(palette[1].position,authored[1].position));
    CHECK(Near(palette[2].position,authored[2].position));
    CHECK(Near(palette[7].position-palette[6].position,{0.1f,0,0}));
    CHECK(Near(palette[10].position-palette[7].position,{0.1f,0,0}));
    CHECK(!Near(palette[3].position,authored[3].position));
    rig.shoulderBackM=.1f;
    CHECK(BuildTrackedFirstPersonPalette(binding,authored,camera,tracking,reference,.5f,true,palette));
    CHECK(Near(palette[1].position,authored[1].position+Vec3{-.1f,0,0}));
    CHECK(Near(palette[2].position,authored[2].position+Vec3{-.1f,0,0}));
    rig.shoulderBackM=0;rig.primaryAim.position.z=-1.5f;
    CHECK(BuildTrackedFirstPersonPalette(binding,authored,camera,tracking,reference,1,true,palette));
    const Vec3 stretchedUpper=palette[4].position-palette[2].position;
    CHECK(std::sqrt(Dot(stretchedUpper,stretchedUpper))>.35f);
    CHECK(Near(palette[6].position,{1.5f,-.3f,0}));
    rig.primaryAim.position.z=-.5f;
    rig.leftHanded=true;
    rig.primaryAim.position={-0.3f,0,-0.5f};rig.support.position={0.3f,0,-0.3f};
    CHECK(BuildTrackedFirstPersonPalette(binding,authored,camera,tracking,reference,1,true,palette));
    CHECK(Near(palette[6].position,{0.5f,0.3f,0}));
    CHECK(Near(palette[5].position,{0.3f,-0.3f,0}));
    CHECK(Near(palette[7].position,{0.6f,0.3f,0}));
    rig.handAlignment=true;
    CHECK(BuildTrackedFirstPersonPalette(binding,authored,camera,tracking,reference,1,true,palette));
    CHECK(Near(palette[5].position,{0.5f,0.3f,0}));
    CHECK(Near(palette[6].position,{0.3f,-0.3f,0}));
    CHECK(Near(palette[7].position,{0.6f,0.3f,0}));
    CHECK(Near(palette[8].position-palette[5].position,{0.05f,0,0}));
    // The support hand uses H3's explicit mirrored presentation trim, while
    // primary aim remains the independently published calibrated controller.
    rig.supportMountYawDeg=30;
    CHECK(BuildTrackedFirstPersonPalette(binding,authored,camera,tracking,reference,1,true,palette));
    CHECK(palette[6].forward.y<-0.49f);
    CHECK(Near(palette[7].forward,{1,0,0}));
    rig.supportMountYawDeg=0;
    rig.floatingHands=true;
    CHECK(BuildTrackedFirstPersonPalette(binding,authored,camera,tracking,reference,1,true,palette));
    for (int index:{1,2,3,4}) CHECK(palette[index].scale<0.0001f);
    for (int index:{0,5,6,7,8,9,10}) CHECK(palette[index].scale==1);
    // Regression: a camera-origin collapse stretches mixed forearm/wrist
    // vertices into spikes. Each hidden arm must instead finish at its own
    // tracked wrist, including anatomical left-handed and two-hand routing.
    for (int index:{1,3}) CHECK(Near(palette[index].position,palette[5].position));
    for (int index:{2,4}) CHECK(Near(palette[index].position,palette[6].position));
    const auto hidden=palette;
    rig.floatingHands=false;
    CHECK(BuildTrackedFirstPersonPalette(binding,authored,camera,tracking,reference,1,true,palette));
    for (int index:{0,5,6,7,8,9,10})
        CHECK(std::memcmp(&palette[index],&hidden[index],sizeof(NodeMatrix))==0);
    rig.floatingHands=true;
    auto invalidBinding=binding;invalidBinding.armMask[0]|=1;
    const auto beforeCollapse=palette;
    CHECK(!CollapseFirstPersonArmsAtWrists(invalidBinding,palette));
    CHECK(std::memcmp(&palette,&beforeCollapse,sizeof(palette))==0);
    rig.gunForwardM=-0.1f;rig.gunRightM=0.02f;rig.gunUpM=0.03f;rig.gunScale=0.5f;
    CHECK(BuildTrackedFirstPersonPalette(binding,authored,camera,tracking,reference,1,true,palette));
    CHECK(Near(palette[5].position,{0.4f,0.28f,0.03f}));
    CHECK(Near(palette[7].position-palette[5].position,{0.05f,0,0}));
    CHECK(palette[5].scale==0.5f&&palette[6].scale==1&&palette[7].scale==0.5f);
    rig.visualPitchDeg=20;
    CHECK(BuildTrackedFirstPersonPalette(binding,authored,camera,tracking,reference,1,true,palette));
    CHECK(palette[7].forward.z>0.3f);
    // Mesh barrel trim never modifies the controller shot/reticle pose.
    CHECK(BuildControllerMatrix(camera,tracking,reference,rig.primaryAim,1,true,target));
    CHECK(Near(target.forward,{1,0,0}));
    {
        // Reproduce the actual mismatch with nonidentity authored gun/wrist
        // bases, lateral/vertical grip offsets, native scales and mount trim.
        // Identity wrist fixtures cannot detect an unrelated support animation
        // being used as the anatomical primary grip.
        auto sample=tracking;
        auto& controls=sample.controllers;
        controls.floatingHands=false;controls.armIk=false;
        controls.twoHandAimActive=false;controls.handAlignment=false;
        controls.gunScale=.65f;controls.supportScale=1.2f;
        controls.visualPitchDeg=17;controls.visualYawDeg=-21;controls.visualRollDeg=9;
        controls.gunForwardM=.07f;controls.gunRightM=.023f;controls.gunUpM=-.031f;
        auto known=binding;
        // The fixture uses the verified AR identity/count/root; names/parents
        // for all twelve real graphs are independently checked by the primary
        // HCEEK fixture generator, rather than guessed from these test nodes.
        known.count=kCeHandAlignmentPlanes[0].graphCount;
        known.nodeIdentity=kCeHandAlignmentPlanes[0].graphIdentity;
        CHECK(FindHandAlignmentPlane(known)==&kCeHandAlignmentPlanes[0]);
        auto wrong=known;wrong.count--;
        CHECK(!FindHandAlignmentPlane(wrong));
        wrong=known;wrong.gun++;
        CHECK(!FindHandAlignmentPlane(wrong));
        wrong=known;wrong.nodeIdentity^=1;
        CHECK(!FindHandAlignmentPlane(wrong));
        std::array<NodeMatrix,kFirstPersonMaxNodes> pose{};
        for (size_t i=0;i<11;++i) pose[i]=authored[i];
        const auto rotated=[](NodeMatrix matrix,float yaw,float pitch)
        {
            const Quat q=Multiply({0,0,std::sin(yaw/2),std::cos(yaw/2)},
                {0,std::sin(pitch/2),0,std::cos(pitch/2)});
            matrix.forward=Rotate(q,{1,0,0});matrix.left=Rotate(q,{0,1,0});matrix.up=Rotate(q,{0,0,1});
            return matrix;
        };
        pose[7]=rotated(pose[7],.61f,-.23f);pose[7].scale=1.3f;
        pose[6]=rotated(pose[6],-.43f,.32f);pose[6].scale=.8f;
        pose[6].position=pose[7].position+TransformDirection(pose[7],{-.18f,.027f,-.041f})*pose[7].scale;
        pose[5]=rotated(pose[5],1.1f,-.37f);pose[5].scale=1.1f;
        pose[5].position=pose[7].position+TransformDirection(pose[7],{.16f,-.035f,-.022f})*pose[7].scale;
        pose[8]=pose[5];pose[8].position=pose[5].position+TransformDirection(pose[5],{.043f,.006f,.013f})*pose[5].scale;
        pose[9]=pose[6];pose[9].position=pose[6].position+TransformDirection(pose[6],{.043f,-.006f,.013f})*pose[6].scale;
        pose[10]=pose[7];pose[10].position=pose[7].position+TransformDirection(pose[7],{.2f,.01f,-.015f})*pose[7].scale;
        std::array<NodeMatrix,kFirstPersonMaxNodes> baseline{},aligned{},changed{};
        CHECK(BuildTrackedFirstPersonPalette(known,pose.data(),camera,sample,reference,.33f,true,baseline));
        controls.handAlignment=true;
        CHECK(BuildTrackedFirstPersonPalette(known,pose.data(),camera,sample,reference,.33f,true,aligned));
        for (int index:{0,7,10}) CHECK(!std::memcmp(&baseline[index],&aligned[index],sizeof(NodeMatrix)));
        const auto expected=[&](const NodeMatrix& wrist,const NodeMatrix& rendered)
        {
            // Independent component expansion of G^-1*W, model-Y reflection,
            // opposite-hand local-Y reflection, and actual rendered G.
            auto localVector=[&](Vec3 value) { return InverseDirection(pose[7],value); };
            const Vec3 local=localVector(wrist.position-pose[7].position)*(1/pose[7].scale);
            NodeMatrix answer{};
            answer.position=rendered.position+TransformDirection(rendered,
                {local.x,-local.y-2*kCeHandAlignmentPlanes[0].offset,local.z})*rendered.scale;
            const Vec3 f=localVector(wrist.forward),l=localVector(wrist.left),u=localVector(wrist.up);
            answer.forward=TransformDirection(rendered,{f.x,-f.y,f.z});
            answer.left=TransformDirection(rendered,{-l.x,l.y,-l.z});
            answer.up=TransformDirection(rendered,{u.x,-u.y,u.z});
            answer.scale=wrist.scale*rendered.scale/pose[7].scale;
            return answer;
        };
        const auto same=[](const NodeMatrix& a,const NodeMatrix& b)
        { return Near(a.position,b.position)&&Near(a.forward,b.forward)&&Near(a.left,b.left)&&Near(a.up,b.up)&&std::fabs(a.scale-b.scale)<.000001f; };
        CHECK(same(aligned[5],expected(pose[6],aligned[7])));
        CHECK(Valid(aligned[5])&&Dot(Cross(aligned[5].forward,aligned[5].left),aligned[5].up)>.999f);
        CHECK(Near(InverseDirection(aligned[5],aligned[8].position-aligned[5].position)*(1/aligned[5].scale),{.043f,.006f,.013f}));
        pose[5]=rotated(pose[5],-.8f,.72f);
        CHECK(BuildTrackedFirstPersonPalette(known,pose.data(),camera,sample,reference,.33f,true,changed));
        CHECK(same(aligned[5],changed[5]));
        CHECK(!std::memcmp(&aligned[7],&changed[7],sizeof(NodeMatrix)));
        controls.supportGripAttached=true;
        controls.twoHandAimActive=true;
        CHECK(BuildTrackedFirstPersonPalette(known,pose.data(),camera,sample,reference,.33f,true,changed));
        CHECK(same(changed[6],expected(pose[5],changed[7])));
        CHECK(!std::memcmp(&aligned[7],&changed[7],sizeof(NodeMatrix)));
        // Persistent support grip: the planted support presentation follows
        // the valid relationship even when support aim authority is rejected.
        // This is the state the old aim-authority gate could not express.
        controls.twoHandAimActive=false;
        CHECK(BuildTrackedFirstPersonPalette(known,pose.data(),camera,sample,reference,.33f,true,changed));
        CHECK(same(changed[6],expected(pose[5],changed[7])));
        CHECK(!std::memcmp(&aligned[7],&changed[7],sizeof(NodeMatrix)));
        controls.supportGripAttached=false;
        controls.twoHandAimActive=false;controls.leftHanded=false;controls.handAlignment=false;
        CHECK(BuildTrackedFirstPersonPalette(known,pose.data(),camera,sample,reference,.33f,true,baseline));
        controls.handAlignment=true;
        CHECK(BuildTrackedFirstPersonPalette(known,pose.data(),camera,sample,reference,.33f,true,changed));
        CHECK(!std::memcmp(&baseline,&changed,sizeof(baseline)));
        // General weapon plane reflection is an involution; all stock planes
        // retain proper rotations and positive native scale, including the
        // non-axis-aligned Needler/Oddball/plasma-cannon roots.
        for (const auto& plane:kCeHandAlignmentPlanes)
        {
            NodeMatrix mirrored{},restored{};
            CHECK(BuildMirroredGripTarget(plane,pose[7],pose[6],pose[7],mirrored));
            CHECK(Valid(mirrored)&&mirrored.scale>0);
            CHECK(BuildMirroredGripTarget(plane,pose[7],mirrored,pose[7],restored));
            CHECK(same(restored,pose[6]));
        }
        auto invalidPlane=kCeHandAlignmentPlanes[0];invalidPlane.normal={0,0,0};
        NodeMatrix output=aligned[5];const auto untouched=output;
        CHECK(!BuildMirroredGripTarget(invalidPlane,pose[7],pose[6],aligned[7],output));
        auto badGun=pose[7];badGun.scale=std::numeric_limits<float>::quiet_NaN();
        CHECK(!BuildMirroredGripTarget(kCeHandAlignmentPlanes[0],badGun,pose[6],aligned[7],output));
        CHECK(!std::memcmp(&output,&untouched,sizeof(output)));
        controls.leftHanded=true;pose[6].scale=std::numeric_limits<float>::infinity();
        const auto previous=changed;
        CHECK(!BuildTrackedFirstPersonPalette(known,pose.data(),camera,sample,reference,.33f,true,changed));
        CHECK(!std::memcmp(&previous,&changed,sizeof(previous)));
    }
    const auto staged=palette;
    rig.controlsPresentationBlocked=true;
    CHECK(!BuildTrackedFirstPersonPalette(binding,authored,camera,tracking,reference,1,true,palette));
    CHECK(std::memcmp(&palette,&staged,sizeof(palette))==0);
    rig.controlsPresentationBlocked=false;
    rig.support.valid=false;
    CHECK(!BuildTrackedFirstPersonPalette(binding,authored,camera,tracking,reference,1,true,palette));
    CHECK(std::memcmp(&palette,&staged,sizeof(palette))==0);
    rig.support.valid=true;rig.gunScale=std::numeric_limits<float>::infinity();
    CHECK(!BuildTrackedFirstPersonPalette(binding,authored,camera,tracking,reference,1,true,palette));
    CHECK(std::memcmp(&palette,&staged,sizeof(palette))==0);
    std::puts("CE first-person: independent wrists/gun, authored grip, reload parts, recenter/generation, and transaction guards passed");
}
