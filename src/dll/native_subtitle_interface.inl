// Shared by production hook and ABI/failure-isolation fixture.
bool CopySubtitleInterfaceText(const wchar_t* source,wchar_t (&copy)[subtitles::kTextCapacity]) noexcept {
    if(!source||(reinterpret_cast<uintptr_t>(source)&1)) return false;
    __try {
        for(size_t i=0;i<subtitles::kTextCapacity;++i) {
            copy[i]=source[i];
            if(!copy[i]) return i!=0;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { }
    return false;
}
bool DispatchNativeSubtitleInterface(uintptr_t caller,void* self,const void* a,const void* b,const void* c,
    const wchar_t* text,float seconds,uint32_t style) {
    const GameTitle title=TitleAdapter_GetActiveTitle();
    const auto index=static_cast<unsigned>(title);
    const bool theatre=VR_IsCutsceneTheaterActive();
    auto* authority=index>0&&index<_countof(g_subtitleAuthorities)?&g_subtitleAuthorities[index]:nullptr;
    const uint64_t revision=authority?authority->revision.load(std::memory_order_acquire):1;
    const uint32_t generation=authority?authority->generation.load(std::memory_order_relaxed):0;
    bool proven=false;uint8_t policy=0;
    if(caller&&authority&&!(revision&1)&&generation&&TitleAdapter_GetGeneration(title)==generation) {
        for(size_t i=0;i<kSubtitleSourceCount;++i)
            if(authority->callers[i].load(std::memory_order_relaxed)==caller) {
                policy=authority->policies[i].load(std::memory_order_relaxed);proven=true;break;
            }
        proven=proven&&authority->revision.load(std::memory_order_acquire)==revision;
    }
    wchar_t copy[subtitles::kTextCapacity]{};
    // The native callee may consume/release its input. Keep only a bounded copy
    // made while the incoming pointer belongs to this invocation.
    const bool copied=proven&&generation&&CopySubtitleInterfaceText(text,copy);
    // Retain all seven native arguments and the native Boolean result. Normal
    // compiler-generated unwind metadata also preserves native exceptions.
    const bool accepted=g_originalInterface(self,a,b,c,text,seconds,style);
    if(accepted && copied && authority->revision.load(std::memory_order_acquire)==revision &&
        TitleAdapter_GetActiveTitle()==title && TitleAdapter_GetGeneration(title)==generation &&
        theatre==VR_IsCutsceneTheaterActive())
        NativeSubtitles_Capture(title,generation,copy,seconds,theatre,policy&3,(policy&4)!=0);
    return accepted;
}
