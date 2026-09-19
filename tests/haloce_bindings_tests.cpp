#include "haloce_native_bindings.h"
#include "haloce_contracts.generated.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
#include <set>
#include <MinHook.h>

using namespace halo_ce;
namespace
{
void WritePattern(std::vector<uint8_t>& image,uint32_t rva,const char* pattern)
{
    while (*pattern)
    {
        if (*pattern!='?')
        {
            unsigned value=0;
            std::sscanf(pattern,"%2x",&value);
            image.at(rva)=static_cast<uint8_t>(value);
        }
        ++rva; pattern+=2;
        if (*pattern==' ') ++pattern;
    }
}
std::vector<uint8_t> Fixture()
{
    std::vector<uint8_t> image(contract::imageSize,0xcc);
    IMAGE_DOS_HEADER dos{};
    dos.e_magic=IMAGE_DOS_SIGNATURE; dos.e_lfanew=0x80;
    std::memcpy(image.data(),&dos,sizeof(dos));
    IMAGE_NT_HEADERS64 nt{};
    nt.Signature=IMAGE_NT_SIGNATURE;
    nt.FileHeader.Machine=IMAGE_FILE_MACHINE_AMD64;
    nt.FileHeader.TimeDateStamp=contract::timestamp;
    nt.FileHeader.NumberOfSections=1;
    nt.FileHeader.SizeOfOptionalHeader=sizeof(IMAGE_OPTIONAL_HEADER64);
    nt.OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    nt.OptionalHeader.SizeOfHeaders=0x1000;
    nt.OptionalHeader.SizeOfImage=contract::imageSize;
    nt.OptionalHeader.NumberOfRvaAndSizes=IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
    auto& unwind=nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    unwind.VirtualAddress=0x1000;
    for (const auto& entry:contract::entries)
    {
        WritePattern(image,entry.rva,entry.pattern);
        if (entry.unwind)
        {
            IMAGE_RUNTIME_FUNCTION_ENTRY function{};
            function.BeginAddress=entry.rva; function.EndAddress=entry.rva+0x80;
            std::memcpy(image.data()+unwind.VirtualAddress+unwind.Size,&function,sizeof(function));
            unwind.Size+=sizeof(function);
        }
    }
    std::memcpy(image.data()+dos.e_lfanew,&nt,sizeof(nt));
    IMAGE_SECTION_HEADER section{};
    section.VirtualAddress=0x1000;
    section.Misc.VirtualSize=section.SizeOfRawData=contract::imageSize-0x1000;
    section.Characteristics=IMAGE_SCN_MEM_EXECUTE|IMAGE_SCN_MEM_READ;
    std::memcpy(image.data()+dos.e_lfanew+sizeof(nt),&section,sizeof(section));
    for (const auto& witness:contract::witnesses) WritePattern(image,witness.rva,witness.pattern);
    for (const auto& relative:contract::relatives)
    {
        const auto displacement=static_cast<int32_t>(relative.target-relative.rva-relative.size);
        std::memcpy(image.data()+relative.rva+relative.displacement,&displacement,4);
    }
    for (const auto& pointer:contract::pointers)
    {
        const uintptr_t target=reinterpret_cast<uintptr_t>(image.data())+pointer.target;
        std::memcpy(image.data()+pointer.rva,&target,sizeof(target));
    }
    return image;
}
}
int main(int argc,char** argv)
{
    int failures=0;
    const auto check=[&](bool ok,const char* name) {
        if (!ok) { std::fprintf(stderr,"CE bindings: %s\n",name); ++failures; }
    };
    auto image=Fixture();
    const auto base=reinterpret_cast<uintptr_t>(image.data());
    NativeBindings bindings{};
    const char* failure=nullptr;
    const auto resolve=[&]() { return ResolveNativeBindings(base,image.size(),4,bindings,failure); };
    check(resolve(),"mapped x64 fixture with every contract admitted");
    if (failure) std::fprintf(stderr,"initial failure: %s\n",failure);
    check(bindings.generation==4&&bindings.viewRebuild==base+0x11aba0&&
        bindings.surfaceSelector==base+0xad5f0,"CE-specific callable addresses returned");
    const contract::Entry optionalEntries[]{
        {"fixture optional feature",0x3000,"0F 0B 31 42 53 64 75 86 97 A8 B9 CA DB EC FD",false}};
    const NativeContractSet optionalSet{optionalEntries,{},{},{}};
    WritePattern(image,optionalEntries[0].rva,optionalEntries[0].pattern);
    check(VerifyNativeFeatureBindings(base,image.size(),4,optionalSet,failure),
        "independent feature signature is verified against the same pinned image");
    image[0x3000]^=1;
    check(!VerifyNativeFeatureBindings(base,image.size(),4,optionalSet,failure)&&resolve(),
        "optional feature failure leaves camera bindings independently usable");
    check(!VerifyNativeFeatureBindings(base,image.size(),4,{},failure),
        "empty optional contract cannot claim proof");
    std::memset(image.data()+0x3000,0xcc,32);
    SaberViewPair native{};
    Tracking tracking{}; tracking.generation=5;
    Reference reference{};
    StagedViewPair staged{}; staged.serial=71;
    check(StageBoundNativePair(bindings,native,tracking,reference,0.33f,true,staged)==
        PairStageResult::InvalidTracking&&staged.serial==71,
        "retired binding generation rejected before any native callback");
    const auto& first=contract::entries[0];
    image[first.rva]^=1;
    check(!resolve()&&!bindings.base,"changed entry clears all prior bindings");
    image[first.rva]^=1;
    WritePattern(image,0x2000,first.pattern);
    check(!resolve(),"second executable match rejected");
    std::memset(image.data()+0x2000,0xcc,0x100);
    const auto& edge=contract::relatives[0];
    image[edge.rva+edge.displacement]^=1;
    check(!resolve(),"retargeted call rejected despite matching entry signature");
    image[edge.rva+edge.displacement]^=1;
    const auto& witness=contract::witnesses[0];
    image[witness.rva]^=1;
    check(!resolve(),"changed body witness rejected");
    image[witness.rva]^=1;
    image[contract::pointers[0].rva]^=8;
    check(!resolve(),"different backend virtual target rejected");
    image[contract::pointers[0].rva]^=8;
    image[0x1000]^=1;
    check(!resolve(),"missing native unwind entry rejected");
    image[0x1000]^=1;
    auto* nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(image.data()+0x80);
    nt->FileHeader.TimeDateStamp^=1;
    check(!resolve(),"different PE identity rejected");
    nt->FileHeader.TimeDateStamp^=1;
    auto* section=reinterpret_cast<IMAGE_SECTION_HEADER*>(image.data()+0x80+sizeof(*nt));
    section->Characteristics&=~IMAGE_SCN_MEM_EXECUTE;
    check(!resolve(),"signatures in non-executable memory rejected");
    section->Characteristics|=IMAGE_SCN_MEM_EXECUTE;
    section->SizeOfRawData=0xffffffff;
    check(!resolve(),"overflowing section range rejected before scanning");
    section->SizeOfRawData=contract::imageSize-0x1000;
    check(resolve(),"restored valid image recovers without latched rejection");
    check(!ResolveNativeBindings(base,image.size(),0,bindings,failure)&&!bindings.base,
        "zero generation cannot reuse an earlier binding");
    check(!ResolveNativeBindings(1,contract::imageSize,4,bindings,failure)&&!bindings.base,
        "unreadable mapping fails safely");

    // Reproduce the installed-order failure with an actual MinHook patch in
    // private fixture memory. No game process or native function is executed.
    const contract::Entry releaseEntry[]{contract::hud_target::entries.back()};
    const NativeContractSet releaseSet{releaseEntry,{},{},{}};
    check(VerifyNativeFeatureBindings(base,image.size(),4,releaseSet,failure),
        "HUD release witness passes before the core hook changes its entry");
    void* trampoline{};
    void* releaseTarget=reinterpret_cast<void*>(base+releaseEntry[0].rva);
    DWORD priorProtection{},ignoredProtection{};
    check(VirtualProtect(releaseTarget,64,PAGE_EXECUTE_READWRITE,&priorProtection)!=0,
        "private fixture page admits MinHook executable-target validation");
    check(MH_Initialize()==MH_OK&&
        MH_CreateHook(releaseTarget,reinterpret_cast<void*>(&main),&trampoline)==MH_OK&&
        MH_EnableHook(releaseTarget)==MH_OK,"private release-hook installation fixture");
    check(!VerifyNativeFeatureBindings(base,image.size(),4,releaseSet,failure)&&
        failure==releaseEntry[0].name,
        "rescan after a real core-style hook reproduces hud_target_release rejection");
    check(MH_DisableHook(releaseTarget)==MH_OK&&MH_RemoveHook(releaseTarget)==MH_OK&&
        MH_Uninitialize()==MH_OK,"private release-hook fixture retires fully");
    check(VirtualProtect(releaseTarget,64,priorProtection,&ignoredProtection)!=0,
        "private fixture page protection restored");
    check(VerifyNativeFeatureBindings(base,image.size(),4,releaseSet,failure),
        "original native witness returns after hook retirement");

    // Optional read-only check of the actual pinned PE. This maps file bytes
    // into NON-EXECUTABLE private storage, rebases only the tested vtable slots,
    // and executes the same production verifier. No LoadLibrary or game code.
    if (argc==3&&std::strcmp(argv[1],"--image")==0)
    {
        std::ifstream file(argv[2],std::ios::binary);
        std::vector<uint8_t> raw((std::istreambuf_iterator<char>(file)),{});
        if (raw.size()<sizeof(IMAGE_DOS_HEADER)) return 2;
        const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(raw.data());
        if (dos->e_lfanew<0||static_cast<size_t>(dos->e_lfanew)>raw.size()-sizeof(IMAGE_NT_HEADERS64)) return 2;
        const auto* source=reinterpret_cast<const IMAGE_NT_HEADERS64*>(raw.data()+dos->e_lfanew);
        if (source->OptionalHeader.SizeOfImage!=contract::imageSize||
            source->OptionalHeader.SizeOfHeaders>raw.size()) return 2;
        image.assign(contract::imageSize,0);
        std::memcpy(image.data(),raw.data(),source->OptionalHeader.SizeOfHeaders);
        const size_t sectionOffset=dos->e_lfanew+sizeof(*source);
        const size_t sectionLength=source->FileHeader.NumberOfSections*sizeof(IMAGE_SECTION_HEADER);
        if (sectionOffset>raw.size()||sectionLength>raw.size()-sectionOffset) return 2;
        const auto* sections=reinterpret_cast<const IMAGE_SECTION_HEADER*>(raw.data()+sectionOffset);
        for (size_t i=0;i<source->FileHeader.NumberOfSections;++i)
        {
            const auto& s=sections[i];
            if (s.PointerToRawData>raw.size()||s.SizeOfRawData>raw.size()-s.PointerToRawData||
                s.VirtualAddress>image.size()||s.SizeOfRawData>image.size()-s.VirtualAddress) return 2;
            std::memcpy(image.data()+s.VirtualAddress,raw.data()+s.PointerToRawData,s.SizeOfRawData);
        }
        const auto mappedBase=reinterpret_cast<uintptr_t>(image.data());
        const NativeContractSet featureSets[]{
            {contract::audio_listener::entries,contract::audio_listener::witnesses,contract::audio_listener::relatives,contract::audio_listener::pointers},
            {contract::unit_control::entries,contract::unit_control::witnesses,contract::unit_control::relatives,contract::unit_control::pointers},
            {contract::network_input::entries,contract::network_input::witnesses,contract::network_input::relatives,contract::network_input::pointers},
            {contract::vehicle::entries,contract::vehicle::witnesses,contract::vehicle::relatives,contract::vehicle::pointers},
            {contract::camera_effect::entries,contract::camera_effect::witnesses,contract::camera_effect::relatives,contract::camera_effect::pointers},
            {contract::flare_guard::entries,contract::flare_guard::witnesses,contract::flare_guard::relatives,contract::flare_guard::pointers},
            {contract::contact::entries,contract::contact::witnesses,contract::contact::relatives,contract::contact::pointers},
            {contract::anniversary_resolution::entries,contract::anniversary_resolution::witnesses,contract::anniversary_resolution::relatives,contract::anniversary_resolution::pointers},
            {contract::anniversary_hud::entries,contract::anniversary_hud::witnesses,contract::anniversary_hud::relatives,contract::anniversary_hud::pointers},
            {contract::gameplay_bridge::entries,contract::gameplay_bridge::witnesses,contract::gameplay_bridge::relatives,contract::gameplay_bridge::pointers},
            {contract::comfort::entries,contract::comfort::witnesses,contract::comfort::relatives,contract::comfort::pointers},
            {contract::entries,contract::witnesses,contract::relatives,contract::pointers},
            {contract::classic::entries,contract::classic::witnesses,contract::classic::relatives,contract::classic::pointers},
            {contract::classic_first_person_projection::entries,contract::classic_first_person_projection::witnesses,contract::classic_first_person_projection::relatives,contract::classic_first_person_projection::pointers},
            {contract::first_person::entries,contract::first_person::witnesses,contract::first_person::relatives,contract::first_person::pointers},
            {contract::first_person_aim::entries,contract::first_person_aim::witnesses,contract::first_person_aim::relatives,contract::first_person_aim::pointers},
            {contract::first_person_skin::entries,contract::first_person_skin::witnesses,contract::first_person_skin::relatives,contract::first_person_skin::pointers},
            {contract::first_person_projection::entries,contract::first_person_projection::witnesses,contract::first_person_projection::relatives,contract::first_person_projection::pointers},
            {contract::first_person_visibility::entries,contract::first_person_visibility::witnesses,contract::first_person_visibility::relatives,contract::first_person_visibility::pointers},
            {contract::first_person_particles::entries,contract::first_person_particles::witnesses,contract::first_person_particles::relatives,contract::first_person_particles::pointers},
            {contract::hud::entries,contract::hud::witnesses,contract::hud::relatives,contract::hud::pointers},
            {contract::hud_layout::entries,contract::hud_layout::witnesses,contract::hud_layout::relatives,contract::hud_layout::pointers},
            {contract::hud_target::entries,contract::hud_target::witnesses,contract::hud_target::relatives,contract::hud_target::pointers},
            {contract::player_state::entries,contract::player_state::witnesses,contract::player_state::relatives,contract::player_state::pointers},
            {contract::controls::entries,contract::controls::witnesses,contract::controls::relatives,contract::controls::pointers}};
        std::set<uint32_t> relocatedPointers;
        for (const auto& set:featureSets) for (const auto& pointer:set.pointers)
        {
            if (!relocatedPointers.insert(pointer.rva).second) continue;
            uintptr_t original{};
            std::memcpy(&original,image.data()+pointer.rva,8);
            const uintptr_t relocated=original-source->OptionalHeader.ImageBase+mappedBase;
            std::memcpy(image.data()+pointer.rva,&relocated,8);
        }
        check(ResolveNativeBindings(mappedBase,image.size(),9,bindings,failure),
            "production mapped-image verifier accepts the pinned PE offline");
        if (failure) std::fprintf(stderr,"pinned failure: %s\n",failure);
        for (const auto& set:featureSets)
        {
            check(VerifyNativeFeatureBindings(mappedBase,image.size(),9,set,failure),
                "production optional-feature verifier accepts its pinned native group");
            if (failure) std::fprintf(stderr,"pinned feature failure: %s\n",failure);
        }
        // A9A8A4 is a leaf/tail-jump without an unwind entry. Verify actual
        // MinHook relocation of its pinned prologue in private mapped bytes;
        // never execute native code or touch an installed module.
        void* actionTarget=image.data()+contract::network_input::action_build;
        void* actionTrampoline{};DWORD protection{},unused{};
        check(VirtualProtect(actionTarget,64,PAGE_EXECUTE_READWRITE,&protection)!=0,
            "private action entry gets temporary executable fixture protection");
        check(MH_Initialize()==MH_OK&&MH_CreateHook(actionTarget,reinterpret_cast<void*>(&main),&actionTrampoline)==MH_OK&&
            MH_EnableHook(actionTarget)==MH_OK,"pinned leaf action builder supports MinHook trampoline");
        check(MH_DisableHook(actionTarget)==MH_OK&&MH_RemoveHook(actionTarget)==MH_OK&&MH_Uninitialize()==MH_OK,
            "pinned action hook fixture retires exactly");
        check(VirtualProtect(actionTarget,64,protection,&unused)!=0,"private action fixture protection restored");
    }
    return failures?1:0;
}
