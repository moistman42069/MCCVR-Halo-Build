#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

// Small, title-specific packet transaction used after H4's first-person
// producer has returned. Row classification, owner admission, candidate
// solving, and guarded memory access remain supplied by the H4 adapter.
namespace halo4_body_packet {
using ProducerFn=uintptr_t(__fastcall*)(uint32_t,uint64_t,uint32_t,void*);
using AfterProducerFn=void(*)(void*,uint32_t,uint64_t,uint32_t,void*,uintptr_t);
inline uintptr_t InvokeNativeOnceAndThen(ProducerFn original,
    AfterProducerFn after,void* context,uint32_t user,uint64_t unit,
    uint32_t maximum,void* records)
{
    if(!original)return 0;
    const uintptr_t emitted=original(user,unit,maximum,records);
    if(after)after(context,user,unit,maximum,records,emitted);
    return emitted;
}
enum class RowKind : uint8_t { None, Body, Hands };
enum class Result : uint8_t {
    Applied, Ineligible, MissingRows, AmbiguousRows, OwnerMismatch,
    PreviewRejected, StalePair, ReadFailure, WriteFailure, RollbackFailure
};

template<size_t MatrixBytes,class Read,class Write,class Classify,
    class Admit,class Solve,class StillCurrent>
Result Apply(uint32_t userIndex,uint64_t unit,uint32_t maxRecords,
    void* outputRecords,uintptr_t emittedRecords,uint32_t recordLimit,
    size_t stride,size_t modelOffset,size_t unitOffset,size_t fillOffset,
    size_t maskOffset,size_t matrixOffset,Read&& read,Write&& write,
    Classify&& classify,Admit&& admit,Solve&& solve,
    StillCurrent&& stillCurrent) noexcept
{
    if(userIndex!=0||!outputRecords||maxRecords==0||maxRecords>recordLimit||
        emittedRecords==0||emittedRecords>maxRecords||stride==0)
        return Result::Ineligible;
    auto* const base=static_cast<uint8_t*>(outputRecords);
    uint8_t* body=nullptr;uint8_t* hands=nullptr;
    uint32_t bodyMask=0,handsMask=0;
    for(uintptr_t i=0;i<emittedRecords;++i)
    {
        uint8_t* row=base+i*stride;
        uint16_t model=0;uint32_t rowUnit=UINT32_MAX,mask=0;uint8_t fill=0xFF;
        if(!read(row+modelOffset,&model,sizeof(model))||
           !read(row+unitOffset,&rowUnit,sizeof(rowUnit))||
           !read(row+fillOffset,&fill,sizeof(fill))||
           !read(row+maskOffset,&mask,sizeof(mask)))return Result::ReadFailure;
        if(rowUnit!=unit)continue;
        RowKind kind=RowKind::None;
        if(!classify(model,fill,kind))continue;
        if(kind==RowKind::Body) {
            if(body)return Result::AmbiguousRows;
            body=row;bodyMask=mask;
        } else if(kind==RowKind::Hands) {
            if(hands)return Result::AmbiguousRows;
            hands=row;handsMask=mask;
        }
    }
    if(!body||!hands)return Result::MissingRows;
    if(!admit(unit))return Result::OwnerMismatch;

    std::array<uint8_t,MatrixBytes> before{},candidate{};
    uint32_t nextBodyMask=bodyMask,nextHandsMask=handsMask;
    if(!read(body+matrixOffset,before.data(),before.size()))return Result::ReadFailure;
    if(!solve(hands+matrixOffset,before.data(),bodyMask,handsMask,
        candidate.data(),nextBodyMask,nextHandsMask))return Result::PreviewRejected;
    if(!stillCurrent(unit))return Result::StalePair;

    uint8_t* const bodyMatrix=body+matrixOffset;
    uint8_t* const bodyMaskPtr=body+maskOffset;
    uint8_t* const handsMaskPtr=hands+maskOffset;
    const bool matrixWritten=write(bodyMatrix,candidate.data(),candidate.size());
    const bool bodyMaskWritten=matrixWritten&&write(bodyMaskPtr,&nextBodyMask,sizeof(nextBodyMask));
    const bool handsMaskWritten=bodyMaskWritten&&write(handsMaskPtr,&nextHandsMask,sizeof(nextHandsMask));
    if(matrixWritten&&bodyMaskWritten&&handsMaskWritten)return Result::Applied;

    // Writes may fail after partially changing their target. Restore every
    // transaction field, including the failing field, from the before-image.
    const bool handsRestored=write(handsMaskPtr,&handsMask,sizeof(handsMask));
    const bool bodyRestored=write(bodyMaskPtr,&bodyMask,sizeof(bodyMask));
    const bool matrixRestored=write(bodyMatrix,before.data(),before.size());
    return handsRestored&&bodyRestored&&matrixRestored
        ?Result::WriteFailure:Result::RollbackFailure;
}
} // namespace halo4_body_packet
