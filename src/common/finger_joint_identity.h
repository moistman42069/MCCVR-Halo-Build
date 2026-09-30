#pragma once
#include "runtime_types.h"
#include <cstddef>
#include <cstdint>

// Identity only: translations remain in the admitted palette's coordinate
// frame. This does not label model-space nodes as world-space contacts.
namespace finger_joint {
enum class Palette : uint8_t { FirstPerson, WorldBody };
enum class Hand : uint8_t { Left, Right };
enum class Digit : uint8_t { Unknown, Thumb, Index, Middle, Ring, Pinky };
enum class Joint : uint8_t { Root, Child1, Child2, Child3 };
enum class AnatomicalJoint : uint8_t { Unknown, Metacarpal, Proximal, Intermediate, Distal };
struct RigKey {
    GameTitle title=GameTitle::None;
    uint64_t checksum=0;
    uint16_t nodeCount=0;
    Palette palette=Palette::FirstPerson;
    bool operator==(const RigKey&) const = default;
};
struct Record {
    RigKey rig{};
    Hand hand=Hand::Left;
    Digit digit=Digit::Unknown;
    uint8_t nativeDigitSlot=0,jointOrdinal=0;
    Joint joint=Joint::Root;
    uint16_t paletteIndex=0,parentIndex=0;
    AnatomicalJoint anatomicalJoint=AnatomicalJoint::Unknown;
    // Populated only when a title proves a separate source-to-output bone map.
    uint16_t sourcePaletteIndex=UINT16_MAX;
};
struct Inventory {
    RigKey rig{};Hand hand=Hand::Left;
    Record records[20]{};uint8_t count=0;
    const Record* FindNative(unsigned slot,unsigned ordinal) const noexcept {
        for(unsigned i=0;i<count;++i)
            if(records[i].nativeDigitSlot==slot&&records[i].jointOrdinal==ordinal)return records+i;
        return nullptr;
    }
    const Record* Find(Digit digit,Joint joint) const noexcept {
        if(digit==Digit::Unknown)return nullptr;
        for(unsigned i=0;i<count;++i)
            if(records[i].digit==digit&&records[i].joint==joint)return records+i;
        return nullptr;
    }
};
inline constexpr Digit NamedDigits[5]{Digit::Thumb,Digit::Index,Digit::Middle,Digit::Ring,Digit::Pinky};
template<class Node,class Length,size_t N>
inline bool Build(RigKey key,unsigned side,unsigned wrist,const int16_t* parents,
    const Node (&chains)[5][N],const Length (&lengths)[5],
    const Digit (&digits)[5],Inventory& output) noexcept {
    if(key.title==GameTitle::None||key.title==GameTitle::Unknown||!key.checksum||!key.nodeCount||side>1||
       !parents||wrist>=key.nodeCount)return false;
    Inventory candidate{};candidate.rig=key;candidate.hand=side?Hand::Right:Hand::Left;
    for(unsigned slot=0;slot<5;++slot) {
        if(lengths[slot]>N||lengths[slot]>4)return false;
        for(unsigned ordinal=0;ordinal<lengths[slot];++ordinal) {
            const unsigned node=chains[slot][ordinal];
            const unsigned parent=ordinal?chains[slot][ordinal-1]:wrist;
            if(node>=key.nodeCount||parents[node]<0||unsigned(parents[node])!=parent)return false;
            for(unsigned i=0;i<candidate.count;++i)if(candidate.records[i].paletteIndex==node)return false;
            candidate.records[candidate.count++]={key,candidate.hand,digits[slot],uint8_t(slot),uint8_t(ordinal),
                static_cast<Joint>(ordinal),uint16_t(node),uint16_t(parent)};
        }
    }
    if(!candidate.count)return false;
    output=candidate;return true;
}
}
