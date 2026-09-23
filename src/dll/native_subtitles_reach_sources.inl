// Worker-only, read-only source proof. Native host observer is process lifetime;
// this feature never patches or retains a title module. HREK semantics and current
// retail/host caller graph are recorded in RENDER-CONTRIBUTIONS-2026-09-23.md.
constexpr char kNativeReachSubtitleGameplayPattern[] =
    "48 8B 03 48 8D 4C 24 60 44 89 74 24 30 4C 8D 44 24 40 F3 0F 11 74 24 28 "
    "48 8D 54 24 50 48 89 4C 24 20 45 33 C9 48 8B CB FF 90 C8 02 00 00";
constexpr char kNativeReachSubtitleRendererPattern[] =
    "40 55 41 54 41 55 41 56 41 57 48 8D AC 24 D0 EB FF FF B8 30 15 00 00 "
    "E8 ?? ?? ?? ?? 48 2B E0 48 C7 44 24 58 FE FF FF FF";
constexpr char kNativeReachSubtitleTheatrePattern[] =
    "48 8B 01 C7 44 24 30 02 00 00 00 F3 0F 11 74 24 28 48 8D 55 90 "
    "48 89 54 24 20 4C 8D 4C 24 60 4C 8D 44 24 78 48 8D 54 24 68 FF 90 C8 02 00 00";
constexpr char kNativeReachSubtitleResolverCall[] =
    "4C 8D 85 F0 09 00 00 8B 8E 3C 06 00 00 E8 ?? ?? ?? ?? BF 00 04 00 00";
constexpr char kNativeReachSubtitleResolver[] =
    "48 89 5C 24 20 55 56 57 41 56 41 57 B8 40 10 00 00 E8 ?? ?? ?? ?? "
    "48 2B E0 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 84 24 30 10 00 00 49 8B F0 8B F9 8B EA";

uintptr_t NativeReachSubtitleSourceUnique(uintptr_t base,size_t size,const char* pattern) {
    const uintptr_t found=sig::Find(base,size,pattern);
    return found && !sig::Find(found+1,base+size-found-1,pattern)?found:0;
}
void NativeReachSubtitles_Poll(uintptr_t base,size_t size,uint32_t generation,bool levelRunning) {
    static uint32_t attempted=0,published=0;
    static uintptr_t publishedBase=0;
    const bool eligible=levelRunning && base && generation &&
        TitleAdapter_GetActiveTitle()==GameTitle::HaloReach &&
        TitleAdapter_GetGeneration(GameTitle::HaloReach)==generation &&
        g_reachCamera.armed.load(std::memory_order_acquire) &&
        !g_reachCamera.teardownRequested.load(std::memory_order_acquire);
    if(!eligible || (published && (published!=generation || publishedBase!=base))) {
        NativeSubtitles_SetReachSources(0,0,0);
        if(published) LOG("Native subtitles: Reach caller authority retired; host observer remains stock forwarding");
        attempted=published=0;publishedBase=0;
        if(!eligible) return;
    }
    if(attempted==generation) return;
    const ReachModuleEpoch epoch{base,generation};
    if(!ReachRenderCandidate_IsPreflightCurrent(ReachRenderCandidate_GetPreflight(epoch))) return;
    attempted=generation;
    const uintptr_t gameplay=NativeReachSubtitleSourceUnique(base,size,kNativeReachSubtitleGameplayPattern);
    const uintptr_t renderer=NativeReachSubtitleSourceUnique(base,size,kNativeReachSubtitleRendererPattern);
    const uintptr_t theatre=NativeReachSubtitleSourceUnique(base,size,kNativeReachSubtitleTheatrePattern);
    const uintptr_t resolveCall=NativeReachSubtitleSourceUnique(base,size,kNativeReachSubtitleResolverCall);
    const uintptr_t resolver=NativeReachSubtitleSourceUnique(base,size,kNativeReachSubtitleResolver);
    const bool gameplayProof=gameplay && gameplay-base==0x711335;
    const bool theatreProof=renderer && theatre && resolveCall && resolver &&
        theatre==renderer+0x642 && resolveCall==renderer+0x36F &&
        sig::RipTarget(resolveCall+0x0E,resolveCall+0x12)==resolver;
    if(!gameplayProof && !theatreProof) {
        LOG("Native subtitles: Reach gameplay/theatre final-call proofs unavailable; stock retained");return;
    }
    const uintptr_t gameplayReturn=gameplayProof?gameplay+0x28+6:0;
    const uintptr_t theatreReturn=theatreProof?theatre+0x29+6:0;
    NativeSubtitles_SetReachSources(generation,gameplayReturn,theatreReturn);
    published=generation;publishedBase=base;
    LOG("Native subtitles: Reach localized caller authority ready (generation=%u gameplay=%d theatre=%d); title image unmodified",
        generation,gameplayProof?1:0,theatreProof?1:0);
}
