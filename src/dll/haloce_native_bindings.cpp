#include "haloce_native_bindings.h"
#include "../common/haloce_contracts.generated.h"
#include <windows.h>
#include <cstring>
#include <iterator>
#include <limits>

namespace halo_ce
{
namespace
{
// Room for the longest pinned pattern with margin. It was 96: the crouch camera's 118-byte entry and a
// 110-byte witness could never parse, so that whole contract read as unverified and CE's crouch camera
// stayed on StockFallback on every build ("CE physical crouch camera: StockFallback; crouch_interpolation").
struct Pattern
{
    uint8_t bytes[256]{},mask[256]{};
    size_t size{};
};
int Hex(char c) noexcept
{
    if (c>='0'&&c<='9') return c-'0';
    if (c>='A'&&c<='F') return c-'A'+10;
    if (c>='a'&&c<='f') return c-'a'+10;
    return -1;
}
bool Parse(const char* text,Pattern& out) noexcept
{
    out={};
    bool constrained=false;
    while (*text)
    {
        if (out.size==std::size(out.bytes)||!text[1]) return false;
        if (text[0]=='?'&&text[1]=='?') { }
        else
        {
            const int hi=Hex(text[0]),lo=Hex(text[1]);
            if (hi<0||lo<0) return false;
            out.bytes[out.size]=static_cast<uint8_t>(hi*16+lo);
            out.mask[out.size]=0xff;
            constrained=true;
        }
        ++out.size;
        text+=2;
        if (*text==' ') ++text;
        else if (*text) return false;
    }
    return out.size&&constrained;
}
bool Matches(const uint8_t* bytes,const Pattern& pattern) noexcept
{
    for (size_t i=0;i<pattern.size;++i)
        if ((bytes[i]&pattern.mask[i])!=pattern.bytes[i]) return false;
    return true;
}
bool Range(size_t size,size_t offset,size_t length) noexcept
{
    return offset<=size&&length<=size-offset;
}
struct Image
{
    const uint8_t* bytes{};
    size_t size{};
    IMAGE_SECTION_HEADER sections[32]{};
    size_t count{};
    uint32_t unwindRva{},unwindSize{};

    bool FileBacked(uint32_t rva,size_t length,bool executable) const noexcept
    {
        if (!Range(size,rva,length)) return false;
        for (size_t i=0;i<count;++i)
        {
            const auto& s=sections[i];
            if (rva>=s.VirtualAddress&&
                Range(s.SizeOfRawData,rva-s.VirtualAddress,length)&&
                (!executable||(s.Characteristics&IMAGE_SCN_MEM_EXECUTE))) return true;
        }
        return false;
    }
    bool Entry(uint32_t rva) const noexcept
    {
        for (size_t i=0;i<unwindSize/sizeof(IMAGE_RUNTIME_FUNCTION_ENTRY);++i)
        {
            IMAGE_RUNTIME_FUNCTION_ENTRY entry{};
            std::memcpy(&entry,bytes+unwindRva+i*sizeof(entry),sizeof(entry));
            if (entry.BeginAddress==rva&&entry.EndAddress>rva&&
                FileBacked(rva,entry.EndAddress-rva,true)) return true;
        }
        return false;
    }
};
bool ReadImage(uintptr_t base,size_t size,Image& image) noexcept
{
    if (!base||size!=contract::imageSize||
        base>std::numeric_limits<uintptr_t>::max()-size) return false;
    const auto* bytes=reinterpret_cast<const uint8_t*>(base);
    IMAGE_DOS_HEADER dos{};
    std::memcpy(&dos,bytes,sizeof(dos));
    if (dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<sizeof(dos)||
        !Range(size,static_cast<size_t>(dos.e_lfanew),sizeof(IMAGE_NT_HEADERS64))) return false;
    IMAGE_NT_HEADERS64 nt{};
    std::memcpy(&nt,bytes+dos.e_lfanew,sizeof(nt));
    if (nt.Signature!=IMAGE_NT_SIGNATURE||nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64||
        nt.FileHeader.TimeDateStamp!=contract::timestamp||
        nt.FileHeader.SizeOfOptionalHeader!=sizeof(IMAGE_OPTIONAL_HEADER64)||
        nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC||
        nt.OptionalHeader.SizeOfImage!=size||!nt.FileHeader.NumberOfSections||
        nt.FileHeader.NumberOfSections>std::size(image.sections)||
        nt.OptionalHeader.NumberOfRvaAndSizes<=IMAGE_DIRECTORY_ENTRY_EXCEPTION) return false;
    const size_t sectionOffset=static_cast<size_t>(dos.e_lfanew)+sizeof(nt);
    const size_t sectionBytes=nt.FileHeader.NumberOfSections*sizeof(IMAGE_SECTION_HEADER);
    if (!Range(size,sectionOffset,sectionBytes)||
        nt.OptionalHeader.SizeOfHeaders<sectionOffset+sectionBytes) return false;
    image.bytes=bytes; image.size=size; image.count=nt.FileHeader.NumberOfSections;
    std::memcpy(image.sections,bytes+sectionOffset,sectionBytes);
    for (size_t i=0;i<image.count;++i)
    {
        const auto& s=image.sections[i];
        if (s.VirtualAddress<nt.OptionalHeader.SizeOfHeaders||
            !Range(size,s.VirtualAddress,s.SizeOfRawData)||
            !Range(size,s.VirtualAddress,s.Misc.VirtualSize)) return false;
        for (size_t j=0;j<i;++j)
        {
            const auto& prior=image.sections[j];
            const size_t end=s.VirtualAddress+std::max(s.Misc.VirtualSize,s.SizeOfRawData);
            const size_t priorEnd=prior.VirtualAddress+std::max(prior.Misc.VirtualSize,prior.SizeOfRawData);
            if (s.VirtualAddress<priorEnd&&prior.VirtualAddress<end) return false;
        }
    }
    const auto& directory=nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    image.unwindRva=directory.VirtualAddress; image.unwindSize=directory.Size;
    return image.unwindSize&&image.unwindSize%sizeof(IMAGE_RUNTIME_FUNCTION_ENTRY)==0&&
        image.FileBacked(image.unwindRva,image.unwindSize,false);
}
bool Verify(uintptr_t base,size_t size,const NativeContractSet& contracts,const char*& failure) noexcept
{
    Image image{};
    failure="mapped PE identity/range";
    if (!ReadImage(base,size,image)) return false;
    if (contracts.entries.empty()) { failure="empty feature contract"; return false; }
    for (const auto& entry:contracts.entries)
    {
        failure=entry.name;
        Pattern pattern{};
        if (!Parse(entry.pattern,pattern)||!image.FileBacked(entry.rva,pattern.size,true)||
            !Matches(image.bytes+entry.rva,pattern)||(entry.unwind&&!image.Entry(entry.rva))) return false;
        unsigned hits=0;
        for (size_t i=0;i<image.count;++i)
        {
            const auto& s=image.sections[i];
            if (!(s.Characteristics&IMAGE_SCN_MEM_EXECUTE)||s.SizeOfRawData<pattern.size) continue;
            // All pinned entry signatures have a fixed first byte. memchr
            // bounds this cold scan without allocating or examining data/BSS.
            if (!pattern.mask[0]) return false;
            const auto* cursor=image.bytes+s.VirtualAddress;
            const auto* end=cursor+s.SizeOfRawData-pattern.size+1;
            while (cursor<end)
            {
                cursor=static_cast<const uint8_t*>(std::memchr(cursor,pattern.bytes[0],end-cursor));
                if (!cursor) break;
                if (Matches(cursor,pattern)&&++hits>1) return false;
                ++cursor;
            }
        }
        if (hits!=1) return false;
    }
    failure="body instruction witness";
    for (const auto& witness:contracts.witnesses)
    {
        Pattern pattern{};
        if (!Parse(witness.pattern,pattern)||!image.FileBacked(witness.rva,pattern.size,true)||
            !Matches(image.bytes+witness.rva,pattern)) return false;
    }
    failure="relative call/data operand";
    for (const auto& edge:contracts.relatives)
    {
        if (!image.FileBacked(edge.rva,edge.size,true)||
            !Range(edge.size,edge.displacement,sizeof(int32_t))) return false;
        int32_t displacement{};
        std::memcpy(&displacement,image.bytes+edge.rva+edge.displacement,sizeof(displacement));
        const int64_t target=static_cast<int64_t>(edge.rva)+edge.size+displacement;
        if (target!=edge.target||!Range(size,edge.target,1)) return false;
    }
    failure="relocated backend vtable pointer";
    for (const auto& pointer:contracts.pointers)
    {
        uintptr_t address{};
        if (!image.FileBacked(pointer.rva,sizeof(address),false)) return false;
        std::memcpy(&address,image.bytes+pointer.rva,sizeof(address));
        if (address!=base+pointer.target) return false;
    }
    failure=nullptr;
    return true;
}
}

bool VerifyNativeFeatureBindings(uintptr_t base,size_t size,uint32_t generation,
    const NativeContractSet& contracts,const char*& failure) noexcept
{
    failure="zero module generation";
    if (!generation) return false;
    __try { return Verify(base,size,contracts,failure); }
    __except(EXCEPTION_EXECUTE_HANDLER)
    { failure="unreadable mapped feature module"; return false; }
}

bool ResolveNativeBindings(uintptr_t base,size_t size,uint32_t generation,
    NativeBindings& out,const char*& failure) noexcept
{
    out={};
    failure="zero module generation";
    if (!generation) return false;
    __try
    {
        const NativeContractSet contracts{contract::entries,contract::witnesses,
            contract::relatives,contract::pointers};
        if (!Verify(base,size,contracts,failure)) return false;
        out={base,size,generation,
            base+contract::anniversary_camera_view_rebuild,
            base+contract::anniversary_camera_projection_rebuild,
            base+contract::anniversary_camera_culling_rebuild,
            base+contract::anniversary_view_prepare,
            base+contract::anniversary_primary_view_pair_builder,
            base+contract::anniversary_full_frame_not_replayable,
            base+contract::anniversary_per_view_output,
            base+contract::anniversary_surface_transfer,
            base+contract::anniversary_surface_selector};
        return true;
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        failure="unreadable mapped module";
        return false;
    }
}

bool RebuildNativeCamera(const NativeBindings& bindings,SaberCamera& camera) noexcept
{
    Camera checked{};
    if (!bindings.base||bindings.size!=contract::imageSize||!bindings.generation||
        !NativeCameraFromSaber(camera,checked)||
        !std::isfinite(camera.horizontalFovDegrees)||camera.horizontalFovDegrees<=0||
        camera.horizontalFovDegrees>=179||
        bindings.viewRebuild!=bindings.base+contract::anniversary_camera_view_rebuild||
        bindings.projectionRebuild!=bindings.base+contract::anniversary_camera_projection_rebuild||
        bindings.cullingRebuild!=bindings.base+contract::anniversary_camera_culling_rebuild) return false;
    using Rebuild=void(__fastcall*)(SaberCamera*);
    __try
    {
        reinterpret_cast<Rebuild>(bindings.viewRebuild)(&camera);
        reinterpret_cast<Rebuild>(bindings.projectionRebuild)(&camera);
        reinterpret_cast<Rebuild>(bindings.cullingRebuild)(&camera);
        return true;
    }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

PairStageResult StageBoundNativePair(const NativeBindings& bindings,
    const SaberViewPair& source,const Tracking& tracking,const Reference& reference,
    float unitsPerMeter,bool positional,StagedViewPair& out) noexcept
{
    if (!bindings.base||!bindings.generation||bindings.generation!=tracking.generation)
        return PairStageResult::InvalidTracking;
    return StageNativeViewPair(source,tracking,reference,unitsPerMeter,positional,
        [&](SaberCamera& camera) { return RebuildNativeCamera(bindings,camera); },out);
}
}
