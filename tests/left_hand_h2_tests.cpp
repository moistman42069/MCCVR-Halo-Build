#include "../src/common/halo2_render_logic.h"
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>

namespace
{
unsigned checks = 0, failures = 0;
void Check(bool condition, const char* message)
{
    ++checks;
    if (!condition) { ++failures; std::printf("FAIL: %s\n", message); }
}
bool Near(float a, float b) { return std::fabs(a - b) < 0.0003f; }
bool Same(const Halo2FirstPersonTransform& a, const Halo2FirstPersonTransform& b)
{
    if (!Near(a.scale, b.scale)) return false;
    for (int axis = 0; axis < 3; ++axis)
        if (!Near(a.translation[axis], b.translation[axis])) return false;
    for (int value = 0; value < 9; ++value)
        if (!Near(a.rotation[value], b.rotation[value])) return false;
    return true;
}
Halo2FirstPersonTransform Transform(float x, float y, float z, float yaw, float scale = 1)
{
    Halo2FirstPersonTransform out{};
    out.scale = scale; out.translation[0] = x; out.translation[1] = y; out.translation[2] = z;
    const float q[4]{0, 0, std::sin(yaw * 0.5f), std::cos(yaw * 0.5f)};
    Halo2QuaternionToFirstPersonBasis(q, out.rotation);
    return out;
}
Halo2FirstPersonTransform Read(const float* nodes, unsigned node)
{
    Halo2FirstPersonTransform out{};
    Check(Halo2ReadFirstPersonTransform(nodes + node * 13, out), "published node remains valid");
    return out;
}
Halo2FirstPersonTransform Palm(const float* nodes, unsigned node,
    Halo2FirstPersonRigKind rig, bool left)
{
    Halo2FirstPersonTransform marker{}, palm{};
    Check(Halo2AnatomicalGripMarker(rig, left, marker) &&
        Halo2ComposeFirstPersonTransforms(Read(nodes, node), marker, palm), "semantic palm is composable");
    return palm;
}
constexpr unsigned count = 6;
constexpr int32_t remap[count]{0, 1, 2, 3, 4, 5};
constexpr int32_t secondaryRemap[count]{-1, 1, -1, 3, -1, -1};
Halo2FirstPersonArmBinding Binding(Halo2FirstPersonRigKind rig)
{
    Halo2FirstPersonArmBinding out{};
    out.valid = true; out.count = count; out.leftWrist = 1; out.rightWrist = 2;
    out.leftSubtree = (1ull << 1) | (1ull << 3);
    out.rightSubtree = (1ull << 2) | (1ull << 4);
    out.leftDirectChildren = 1ull << 3; out.rigKind = rig;
    return out;
}
struct Packets
{
    float hands[count * 13]{}, gun[2 * 13]{}, secondary[2 * 13]{};
    Packets()
    {
        const Halo2FirstPersonTransform nodes[count]{
            Transform(0, 0, 0, 0), Transform(.4f, .12f, -.03f, -.4f),
            Transform(.35f, -.09f, -.02f, .6f), Transform(.44f, .10f, -.01f, -.2f),
            Transform(.39f, -.06f, .01f, .5f), Transform(.1f, 0, -.1f, 0)};
        for (unsigned i = 0; i < count; ++i) Halo2WriteFirstPersonTransform(nodes[i], hands + 13 * i);
        for (unsigned i = 0; i < 2; ++i)
        {
            Halo2WriteFirstPersonTransform(Transform(.38f + .2f * i, -.06f, .04f, .3f), gun + 13 * i);
            Halo2WriteFirstPersonTransform(Transform(.42f + .2f * i, .15f, -.03f, -.7f), secondary + 13 * i);
        }
    }
};

constexpr unsigned armCount = 17;
constexpr int32_t armRemap[armCount]{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
struct ArmPackets
{
    float hands[armCount * 13]{}, gun[13]{};
    Halo2FirstPersonArmBinding binding{};
    ArmPackets(Halo2FirstPersonRigKind rig, bool degenerateLeft = false)
    {
        int16_t parents[armCount]{-1,0,1,2,3,3,3,3,3,0,9,10,11,11,11,11,11};
        if (rig == Halo2FirstPersonRigKind::Elite) parents[8] = 0;
        uint8_t flags[armCount]{};
        flags[3] = kHalo2ModelFlagLeftHand;
        flags[11] = kHalo2ModelFlagRightHand;
        Check(Halo2BuildFirstPersonArmBinding(flags, parents, armCount, binding),
            "production binding derives both arm chains from the node parent graph");
        Check(binding.rigKind == rig,
            "Chief five-digit and Elite four-digit fixtures classify from their own graph anatomy");
        const Halo2FirstPersonTransform nodes[armCount]{
            Transform(0,0,0,0), Transform(-.60f,.05f,0,.17f),
            Transform(-.20f,.05f,0,-.11f), Transform(.25f,.05f,0,.07f),
            Transform(.26f,.07f,.01f,0), Transform(.26f,.03f,.01f,0),
            Transform(.26f,.05f,.03f,0), Transform(.26f,.05f,-.01f,0),
            Transform(.27f,.05f,0,0), Transform(.15f,-.05f,0,-.13f),
            Transform(.30f,-.05f,0,.09f), Transform(.45f,-.05f,0,-.04f),
            Transform(.46f,-.07f,.01f,0), Transform(.46f,-.03f,.01f,0),
            Transform(.46f,-.05f,.03f,0), Transform(.46f,-.05f,-.01f,0),
            Transform(.47f,-.05f,0,0)};
        for (unsigned i=0; i<armCount; ++i)
            Halo2WriteFirstPersonTransform(nodes[i], hands + 13*i);
        Halo2WriteFirstPersonTransform(Transform(.45f,-.05f,.02f,.03f), gun);
        if (degenerateLeft)
        {
            auto s = Read(hands, 1), e = Read(hands, 2), w = Read(hands, 3);
            e.translation[0] = s.translation[0]; e.translation[1] = s.translation[1];
            e.translation[2] = s.translation[2];
            w.translation[0] = e.translation[0]; w.translation[1] = e.translation[1];
            w.translation[2] = e.translation[2];
            Halo2WriteFirstPersonTransform(e, hands + 2*13);
            Halo2WriteFirstPersonTransform(w, hands + 3*13);
        }
    }
};

void TestFinalPacketArmTransaction(Halo2FirstPersonRigKind rig, bool twoHand,
    bool alignment, bool forceLeftFailure)
{
    Halo2CameraBasis root{}, right{}, left{};
    root.forward[1]=1; root.up[2]=1;
    right.position[0]=.45f; right.position[1]=-.05f;
    right.forward[1]=1; right.up[2]=1;
    left.position[0]=.25f; left.position[1]=.05f;
    left.forward[1]=1; left.up[2]=1;
    ArmPackets handOnly(rig, forceLeftFailure), visible(rig, forceLeftFailure);
    if (forceLeftFailure)
    {
        // Keep the left source chain invalid only for the visible transaction.
        auto s=Read(visible.hands,1), e=Read(visible.hands,2), w=Read(visible.hands,3);
        e.translation[0]=s.translation[0]; e.translation[1]=s.translation[1];
        e.translation[2]=s.translation[2]; w.translation[0]=e.translation[0];
        w.translation[1]=e.translation[1]; w.translation[2]=e.translation[2];
        Halo2WriteFirstPersonTransform(e,visible.hands+2*13);
        Halo2WriteFirstPersonTransform(w,visible.hands+3*13);
    }
    Halo2FinalPacketOwnershipResult hiddenResult{}, visibleResult{};
    const bool hiddenOk=Halo2OwnFinalFirstPersonPackets(handOnly.hands,armCount,
        armRemap,handOnly.binding,handOnly.gun,1,root,right,left,twoHand,1,1,3.048f,
        hiddenResult,alignment,false);
    const bool visibleOk=Halo2OwnFinalFirstPersonPackets(visible.hands,armCount,
        armRemap,visible.binding,visible.gun,1,root,right,left,twoHand,1,1,3.048f,
        visibleResult,alignment,true);
    Check(hiddenOk && visibleOk,"hand and optional-arm production transactions both commit");
    Check(std::memcmp(handOnly.gun,visible.gun,sizeof(handOnly.gun))==0,
        "visible-arm solve leaves the weapon packet byte-identical to hand-only mode");
    for (unsigned node : {3u,4u,5u,6u,7u,8u,11u,12u,13u,14u,15u,16u})
        Check(std::memcmp(handOnly.hands+node*13,visible.hands+node*13,13*sizeof(float))==0,
            "visible-arm solve preserves both final wrists and authored finger packets");
    if (forceLeftFailure)
    {
        Check(!visibleResult.armsSolved,
            "one failed arm rejects both arm edits as a single optional transaction");
        for (unsigned node : {1u,2u,9u,10u})
            Check(visible.hands[node*13]==handOnly.hands[node*13],
                "failed arm solve restores the established collapsed-arm fallback");
    }
    else
    {
        Check(visibleResult.armsSolved,
            "production transaction solves both arms for the verified rig and handedness");
        Check(visible.hands[1*13]!=handOnly.hands[1*13] &&
              visible.hands[9*13]!=handOnly.hands[9*13],
            "successful transaction retains both shoulder chains");
    }
}
}

int main()
{
    {
        constexpr unsigned digitNodes=38;
        int16_t parents[digitNodes]{};uint8_t flags[digitNodes]{};
        for(unsigned i=0;i<digitNodes;++i)parents[i]=-1;
        parents[1]=0;parents[2]=1;parents[3]=2;parents[4]=0;parents[5]=4;parents[6]=5;
        flags[1]=kHalo2ModelFlagLeftArmMember;flags[2]=kHalo2ModelFlagLeftArmMember;
        flags[3]=kHalo2ModelFlagLeftHand|kHalo2ModelFlagLeftArmMember;
        flags[4]=kHalo2ModelFlagPrimary;flags[5]=kHalo2ModelFlagPrimary;
        flags[6]=kHalo2ModelFlagRightHand|kHalo2ModelFlagPrimary;
        for(unsigned f=0;f<5;++f)
        {
            const unsigned l=7+f*3,r=22+f*3;
            parents[l]=3;parents[l+1]=static_cast<int16_t>(l);parents[l+2]=static_cast<int16_t>(l+1);
            parents[r]=6;parents[r+1]=static_cast<int16_t>(r);parents[r+2]=static_cast<int16_t>(r+1);
        }
        parents[37]=6;flags[37]=kHalo2ModelFlagSecondary|kHalo2ModelFlagLocalRoot;
        Halo2FirstPersonArmBinding binding{};
        Check(Halo2BuildFirstPersonArmBinding(flags,parents,digitNodes,binding)&&
            binding.fingerPoseSupported&&binding.fingerCount[0]==5&&binding.fingerCount[1]==5,
            "official Chief-sized parent topology binds free-hand digit chains");
        finger_joint::Inventory leftInventory{},rightInventory{};
        Check(Halo2DescribeFirstPersonFingerJoints(binding,0,leftInventory)&&
            Halo2DescribeFirstPersonFingerJoints(binding,1,rightInventory),
            "H2 exposes a separate first-person joint inventory for each anatomical hand");
        const auto* nativeRoot=leftInventory.FindNative(0,0);
        Check(nativeRoot&&nativeRoot->digit==finger_joint::Digit::Unknown&&
            nativeRoot->joint==finger_joint::Joint::Root&&nativeRoot->paletteIndex==7&&
            nativeRoot->anatomicalJoint==finger_joint::AnatomicalJoint::Unknown&&
            !leftInventory.Find(finger_joint::Digit::Unknown,finger_joint::Joint::Root)&&
            leftInventory.rig.title==GameTitle::Halo2&&
            leftInventory.rig.palette==finger_joint::Palette::FirstPerson,
            "H2 preserves native slot identity without guessing digit or anatomical labels");
        int32_t expandedRemap[digitNodes+1]{};
        for(unsigned i=0;i<digitNodes;++i)expandedRemap[i]=static_cast<int32_t>(i);
        expandedRemap[digitNodes]=-1;
        finger_joint::Inventory expandedInventory{};
        Check(Halo2MapFingerInventoryToPacket(leftInventory,expandedRemap,digitNodes+1,
            expandedInventory)&&expandedInventory.rig.nodeCount==digitNodes+1&&
            expandedInventory.records[0].rig.nodeCount==digitNodes+1,
            "H2 mapping supports different output count and updates each record rig key");
        auto oversizedInventory=leftInventory;oversizedInventory.rig.nodeCount=65;
        finger_joint::Inventory oversizedResult{};
        Check(!Halo2MapFingerInventoryToPacket(oversizedInventory,expandedRemap,digitNodes,
            oversizedResult)&&oversizedResult.count==0,
            "H2 mapping rejects oversized source rigs before indexing fixed maps");
        float hands[digitNodes*13]{};int32_t remap[digitNodes]{};
        for(unsigned i=0;i<digitNodes;++i)
        {
            remap[i]=static_cast<int32_t>(i);
            auto node=Transform(float(i)*.001f,float(i%3)*.002f,float(i%5)*.001f,0);
            Halo2WriteFirstPersonTransform(node,hands+i*13);
        }
        for(unsigned f=0;f<5;++f)
        {
            const unsigned l=7+f*3,r=22+f*3;
            auto a=Transform(.1f+f*.006f,.1f,0,0);
            auto b=Transform(.1f+f*.006f,.12f,.01f,0);
            auto c=Transform(.1f+f*.006f,.14f,.03f,0);
            Halo2WriteFirstPersonTransform(a,hands+l*13);Halo2WriteFirstPersonTransform(b,hands+(l+1)*13);
            Halo2WriteFirstPersonTransform(c,hands+(l+2)*13);
            a.translation[1]=-.1f;b.translation[1]=-.08f;c.translation[1]=-.06f;
            Halo2WriteFirstPersonTransform(a,hands+r*13);Halo2WriteFirstPersonTransform(b,hands+(r+1)*13);
            Halo2WriteFirstPersonTransform(c,hands+(r+2)*13);
        }
        float stock[digitNodes*13]{};std::memcpy(stock,hands,sizeof(hands));
        const auto grip=MakeControllerFingerInput(true,.8f,true,.7f);
        Check(Halo2ApplyFreeSupportFingerGripPose(binding,hands,digitNodes,remap,
            false,false,grip),"H2 grip-only support pose accepts exact free hand");
        bool leftMoved=false,rightMoved=false;
        for(unsigned i=7;i<37;++i)
        {
            const bool changed=std::memcmp(hands+i*13,stock+i*13,13*sizeof(float))!=0;
            if(i<22)leftMoved|=changed;else rightMoved|=changed;
        }
        Check(leftMoved&&!rightMoved&&std::memcmp(hands+37*13,stock+37*13,13*sizeof(float))==0,
            "H2 grip poses only free support digits and preserves primary fingers and gun");
        std::memcpy(hands,stock,sizeof(hands));
        const auto triggerOnly=MakeControllerFingerInput(true,1.0f,true,0.0f);
        Check(Halo2ApplyFreeSupportFingerGripPose(binding,hands,digitNodes,remap,
            false,false,triggerOnly)&&std::memcmp(hands,stock,sizeof(hands))==0,
            "H2 unknown native digits do not guess a digit-specific trigger chain");
        std::memcpy(hands,stock,sizeof(hands));
        Check(Halo2ApplyFreeSupportFingerGripPose(binding,hands,digitNodes,remap,
            true,false,grip),"H2 aligned free-hand pose accepts exchanged support side");
        leftMoved=rightMoved=false;
        for(unsigned i=7;i<37;++i)
        { const bool changed=std::memcmp(hands+i*13,stock+i*13,13*sizeof(float))!=0;
          if(i<22)leftMoved|=changed;else rightMoved|=changed; }
        Check(!leftMoved&&rightMoved,"H2 handed alignment routes free-hand grip pose to the opposite native wrist");
        std::memcpy(hands,stock,sizeof(hands));
        Check(Halo2ApplyFreeSupportFingerGripPose(binding,hands,digitNodes,remap,
            false,true,grip)&&std::memcmp(hands,stock,sizeof(hands))==0,
            "latched H2 support grip preserves authored weapon hold");
        Check(Halo2ApplyFreeSupportFingerGripPose(binding,hands,digitNodes,remap,
            false,false,{})&&std::memcmp(hands,stock,sizeof(hands))==0,
            "unavailable H2 finger input preserves authored pose without stale replay");
        int32_t reverseRemap[digitNodes]{};
        for(unsigned i=0;i<digitNodes;++i)reverseRemap[i]=static_cast<int32_t>(digitNodes-1-i);
        finger_joint::Inventory packetInventory{};
        Check(Halo2MapFingerInventoryToPacket(leftInventory,reverseRemap,digitNodes,
            packetInventory),"H2 provider emits a source-to-output inventory for a valid remap");
        const auto* remappedRoot=packetInventory.FindNative(0,0);
        Check(remappedRoot&&remappedRoot->sourcePaletteIndex==7&&
            remappedRoot->paletteIndex==digitNodes-1-7&&
            remappedRoot->parentIndex==digitNodes-1-3,
            "H2 inventory records source node, output node, and remapped parent independently");
        int32_t incompleteRemap[digitNodes]{};
        for(unsigned i=0;i<digitNodes;++i)incompleteRemap[i]=static_cast<int32_t>(i);
        incompleteRemap[7]=-1;
        finger_joint::Inventory rejectedInventory{};
        Check(!Halo2MapFingerInventoryToPacket(leftInventory,incompleteRemap,digitNodes,
            rejectedInventory)&&rejectedInventory.count==0,
            "H2 incomplete finger-chain output mapping is rejected atomically");
        std::memcpy(hands,stock,sizeof(hands));
        Check(Halo2ApplyFreeSupportFingerGripPose(binding,hands,digitNodes,reverseRemap,
            false,false,grip),"H2 identity inventory follows an admitted nonidentity packet remap");
        std::memcpy(hands,stock,sizeof(hands));
        int32_t duplicateRemap[digitNodes]{};
        for(unsigned i=0;i<digitNodes;++i)duplicateRemap[i]=static_cast<int32_t>(i);
        duplicateRemap[0]=duplicateRemap[1];
        Check(!Halo2ApplyFreeSupportFingerGripPose(binding,hands,digitNodes,duplicateRemap,
            false,false,grip)&&std::memcmp(hands,stock,sizeof(hands))==0,
            "H2 duplicate source mapping rejects the finger transaction without changing packets");
    }
    {
        uint8_t result[0x60]{};
        Halo2BreakableSurfaceIdentity identity{};
        const int32_t type=1,structure=17,feature=5;
        const int16_t material=9;
        std::memcpy(result,&type,sizeof(type));
        std::memcpy(result+0x24,&material,sizeof(material));
        std::memcpy(result+0x3C,&structure,sizeof(structure));
        std::memcpy(result+0x50,&feature,sizeof(feature));
        result[0x58]=8; result[0x59]=3;
        Check(Halo2ReadBreakableSurfaceIdentity(result,sizeof(result),identity) &&
            identity.type==1 && identity.structureIndex==17 && identity.feature==5 &&
            identity.breakableSurfaceIndex==3 && identity.material==9,
            "official H2EK breakable result fields decode into stable world identity");
        const auto retained=identity;
        result[0x58]=0;
        Check(!Halo2ReadBreakableSurfaceIdentity(result,sizeof(result),identity) &&
            !identity.valid,
            "non-breakable world contact does not claim native glass support");
        result[0x58]=8;
        const int32_t unsupportedType=4;
        std::memcpy(result,&unsupportedType,sizeof(unsupportedType));
        Check(!Halo2ReadBreakableSurfaceIdentity(result,sizeof(result),identity) &&
            !identity.valid,
            "object result is never misclassified as breakable world geometry");
        std::memcpy(result,&type,sizeof(type));
        Check(!Halo2ReadBreakableSurfaceIdentity(result,0x5F,identity) &&
            !identity.valid && retained.valid,
            "truncated native result is rejected without preserving stale identity");
    }
    {
        auto shoulder=Transform(0,0,0,0,1.2f);
        auto elbow=Transform(.5f,0,0,0,1.1f);
        const auto sourceShoulder=shoulder,sourceElbow=elbow;
        auto wrist=Transform(1,0,0,0),atLimit=Transform(1.8f,0,0,0);
        Check(Halo2SolveFinalPacketArm(shoulder,elbow,wrist,atLimit) &&
              Near(std::fabs(elbow.translation[0]-shoulder.translation[0]),.9f) &&
              Near(std::fabs(atLimit.translation[0]-elbow.translation[0]),.9f) &&
              Near(shoulder.scale,sourceShoulder.scale)&&Near(elbow.scale,sourceElbow.scale),
            "H2 arm solver applies the shared 1.8 maximum stretch without changing authored node scales");
        shoulder=sourceShoulder;elbow=sourceElbow;
        const auto beforeShoulder=shoulder,beforeElbow=elbow;
        auto beyond=Transform(1.8001f,0,0,0);
        Check(!Halo2SolveFinalPacketArm(shoulder,elbow,wrist,beyond) &&
              Same(shoulder,beforeShoulder)&&Same(elbow,beforeElbow),
            "arm target beyond shared stretch cap leaves both source joints untouched");
    }
    {
        Halo2FirstPersonTransform stock=Transform(.37f,-.21f,.14f,.61f,1.3f);
        Halo2FirstPersonTransform desired=Transform(-.18f,.29f,-.33f,-.47f,.8f);
        const float q[4]{.23f,-.31f,.19f,.89f};
        Halo2QuaternionToFirstPersonBasis(q,stock.rotation);
        Halo2FirstPersonTransform delta{},reconstructed{};
        Check(Halo2BuildFirstPersonWorldDelta(desired,stock,delta) &&
              Halo2ComposeFirstPersonTransforms(delta,stock,reconstructed) &&
              Same(desired,reconstructed),
            "world-delta inverse and composition round-trip nonsymmetric 3D rotations, translation, and scale");
    }
    for (auto rig : {Halo2FirstPersonRigKind::MasterChief, Halo2FirstPersonRigKind::Elite})
    {
        TestFinalPacketArmTransaction(rig,false,false,false);
        TestFinalPacketArmTransaction(rig,true,false,false);
        TestFinalPacketArmTransaction(rig,false,rig==Halo2FirstPersonRigKind::MasterChief,false);
    }
    TestFinalPacketArmTransaction(Halo2FirstPersonRigKind::MasterChief,false,false,true);
    Halo2FirstPersonTransform chiefLeft{}, chiefRight{}, eliteLeft{}, eliteRight{};
    Check(Halo2AnatomicalGripMarker(Halo2FirstPersonRigKind::MasterChief, true, chiefLeft) &&
        Halo2AnatomicalGripMarker(Halo2FirstPersonRigKind::MasterChief, false, chiefRight) &&
        Near(chiefLeft.translation[1], -.008561f) && Near(chiefRight.translation[1], .011516f),
        "independent H2EK Chief markers preserve opposite lateral grip offsets");
    auto invalidMarker = Transform(1, 2, 3, .4f);
    const auto markerBefore = invalidMarker;
    Check(!Halo2AnatomicalGripMarker(Halo2FirstPersonRigKind::Unknown, true, invalidMarker) &&
        std::memcmp(&invalidMarker, &markerBefore, sizeof(invalidMarker)) == 0,
        "unidentified rig cannot publish a guessed grip marker");
    Check(Halo2AnatomicalGripMarker(Halo2FirstPersonRigKind::Elite, true, eliteLeft) &&
        Halo2AnatomicalGripMarker(Halo2FirstPersonRigKind::Elite, false, eliteRight),
        "Elite native left and deliberately derived right markers are available");
    Check(Near(eliteLeft.translation[0], eliteRight.translation[0]) &&
        Near(eliteLeft.translation[1], -eliteRight.translation[1]) &&
        Near(eliteLeft.translation[2], eliteRight.translation[2]), "Elite reflection uses its proven local Y plane");
    for (int col = 0; col < 3; ++col)
        for (int row = 0; row < 3; ++row)
            Check(Near(eliteRight.rotation[3 * col + row], eliteLeft.rotation[3 * col + row] *
                (((col == 1) != (row == 1)) ? -1.0f : 1.0f)), "Elite frame reflection keeps a proper rotation");

    for (auto rig : {Halo2FirstPersonRigKind::MasterChief, Halo2FirstPersonRigKind::Elite})
    for (int renderer = 0; renderer < 2; ++renderer)
    for (bool dual : {false, true})
    for (bool twoHand : {false, true})
    for (float scale : {0.5f, 1.0f, 2.0f})
    {
        const auto binding = Binding(rig);
        Halo2CameraBasis primary{}, support{}, compose{};
        primary.position[0] = -.35f; primary.position[1] = .5f; primary.position[2] = -.1f;
        primary.forward[1] = 1; primary.up[2] = 1;
        support.position[0] = .3f; support.position[1] = .2f; support.position[2] = -.2f;
        support.forward[0] = -1; support.up[2] = 1;
        compose.forward[renderer] = 1; compose.up[2] = 1;
        Packets native{}, corrected{};
        auto own = [&](Packets& packets, bool alignment) {
            Halo2FinalPacketOwnershipResult out{};
            return dual ? Halo2OwnDualFirstPersonPackets(packets.hands, count, remap, binding,
                packets.gun, 2, secondaryRemap, binding, packets.secondary, 2, compose,
                primary, support, scale, .7f * scale, 3.048f, out, alignment) :
                Halo2OwnFinalFirstPersonPackets(packets.hands, count, remap, binding,
                    packets.gun, 2, compose, primary, support, twoHand,
                    scale, .7f * scale, 3.048f, out, alignment);
        };
        Check(own(native, false) && own(corrected, true), "single/dual packet math accepts Chief/Elite under distinct compose bases");
        Check(std::memcmp(native.gun, corrected.gun, sizeof(native.gun)) == 0 &&
            std::memcmp(native.secondary, corrected.secondary, sizeof(native.secondary)) == 0,
            "opt-in hand alignment leaves both gun packets byte-identical");
        Check(Same(Palm(corrected.hands, 1, rig, true), Palm(native.hands, 2, rig, false)) &&
            Same(Palm(corrected.hands, 2, rig, false), Palm(native.hands, 1, rig, true)),
            "actual palms exchange complete solved grips across rotations and unequal scales");
        const auto leftWrist = Read(corrected.hands, 1), oldRight = Read(native.hands, 2);
        float gapSquared = 0;
        for (int axis = 0; axis < 3; ++axis)
            gapSquared += std::pow(leftWrist.translation[axis] - oldRight.translation[axis], 2.0f);
        Check(gapSquared > 0.0000001f, "palm correction does not repeat the displaced-wrist coincidence defect");
        Check(Near(leftWrist.scale, oldRight.scale), "role-specific hand scale survives anatomical routing");
        const auto before = corrected;
        const int32_t duplicate[count]{0, 1, 2, 1, 4, 5};
        Check(!Halo2RouteLeftHandedPacketHands(corrected.hands, count, duplicate, binding,
            duplicate, binding, primary, support) && std::memcmp(&before, &corrected, sizeof(before)) == 0,
            "ambiguous wrist map leaves every packet untouched");
        corrected = native;
        corrected.hands[4 * 13 + 10] = std::numeric_limits<float>::quiet_NaN();
        const auto invalid = corrected;
        Check(!Halo2RouteLeftHandedPacketHands(corrected.hands, count, remap, binding,
            dual ? secondaryRemap : remap, binding, primary, support) &&
            std::memcmp(&invalid, &corrected, sizeof(invalid)) == 0,
            "late invalid finger rejects atomically after valid wrist staging");
    }
    std::printf("Halo 2 left-hand alignment: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
