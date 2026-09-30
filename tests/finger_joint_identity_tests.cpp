#include "../src/common/halo3_avatar_logic.h"
#include "../src/common/odst_finger_pose_logic.h"
#include "../src/common/reach_finger_pose_logic.h"
#include "../src/common/halo4_body_ik_logic.h"
#include "../src/common/halo4_fp_finger_pose_logic.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace {
unsigned checks=0;
void Check(bool value,const char* label){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",label);std::exit(1);}}
void Validate(const finger_joint::Inventory& inventory,GameTitle title,unsigned side,unsigned expected) {
    Check(inventory.count==expected&&inventory.rig.title==title,"exact title/count");
    for(unsigned i=0;i<inventory.count;++i) {
        const auto& joint=inventory.records[i];
        Check(joint.rig==inventory.rig&&joint.hand==(side?finger_joint::Hand::Right:finger_joint::Hand::Left),"record retains rig and anatomical hand");
        Check(joint.paletteIndex<joint.rig.nodeCount&&joint.parentIndex<joint.rig.nodeCount,"bounded palette indices");
        Check(inventory.FindNative(joint.nativeDigitSlot,joint.jointOrdinal)==&joint,"stable native slot lookup");
        Check(joint.anatomicalJoint==finger_joint::AnatomicalJoint::Unknown,"ordinal never guesses anatomical phalanx");
        if(joint.digit!=finger_joint::Digit::Unknown)Check(inventory.Find(joint.digit,joint.joint)==&joint,"named lookup preserves exact joint");
    }
}
}
int main(){
    using namespace finger_joint;
    for(unsigned side=0;side<2;++side) {
        Inventory inventory{};
        for(auto checksum:{0x17121B0Du,0x10140D04u,0x17180910u}) {
            const unsigned count=checksum==0x17121B0D?51:55;
            Check(halo3_avatar::DescribeFingers(checksum,count,side,inventory),"H3 own world graph resolves");
            Validate(inventory,GameTitle::Halo3,side,count==51?15:8);
            Check(inventory.Find(Digit::Index,Joint::Root)->nativeDigitSlot==0,"H3 native first slot is index");
        }
        for(auto checksum:{286525724u,403178001u}) {
            Check(odst_fingers::Describe(checksum,37,side,inventory),"ODST own FP graph resolves");
            Validate(inventory,GameTitle::Halo3ODST,side,15);
            Check(inventory.Find(Digit::Thumb,Joint::Child2)->paletteIndex==(side?36:31),"ODST own thumb terminal index");
        }
        for(auto checksum:{reach_fingers::kSpartanChecksum,reach_fingers::kEliteChecksum}) {
            const unsigned count=checksum==reach_fingers::kSpartanChecksum?47:41;
            Check(reach_fingers::Describe(checksum,count,side,inventory),"Reach own FP graph resolves");
            Validate(inventory,GameTitle::HaloReach,side,count==47?15:12);
            if(count==41)Check(!inventory.Find(Digit::Middle,Joint::Root),"Reach Elite missing digit is not fabricated");
        }
        Check(Halo4DescribeFpFingers(kHalo4StormFpRuntimeImportChecksum,80,side,inventory),"H4 own FP graph resolves");
        Validate(inventory,GameTitle::Halo4,side,16);
        const auto fpKey=inventory.rig;
        Check(halo4_body_ik_detail::DescribeFingers(kHalo4BodyIkImportChecksum,120,side,inventory),"H4 own world graph resolves");
        Validate(inventory,GameTitle::Halo4,side,16);
        Check(!(inventory.rig==fpKey)&&inventory.Find(Digit::Pinky,Joint::Child3),"H4 world/FP namespace and fourth joint remain distinct");
        const auto original=inventory;
        Check(!odst_fingers::Describe(kHalo4BodyIkImportChecksum,120,side,inventory)&&std::memcmp(&inventory,&original,sizeof(inventory))==0,"cross-title graph rejected atomically");
    }
    Inventory retained{};std::memset(&retained,0xA5,sizeof(retained));const auto before=retained;
    const int16_t parents[4]{-1,0,1,0};
    const uint16_t duplicate[5][3]{{1,2,0},{1,0,0},{},{},{}};
    const uint8_t lengths[5]{2,1,0,0,0};
    Check(!Build({GameTitle::Halo3,1,4,Palette::FirstPerson},0,0,parents,duplicate,lengths,NamedDigits,retained)&&std::memcmp(&retained,&before,sizeof(before))==0,"duplicate joint identity fails without publishing partial inventory");
    Check(!(RigKey{GameTitle::HaloCE,0x100000001ull,4,Palette::FirstPerson}==RigKey{GameTitle::HaloCE,1,4,Palette::FirstPerson}),"full 64-bit graph identity is retained");
    std::printf("PASS: %u labeled finger-joint identity checks\n",checks);
}
