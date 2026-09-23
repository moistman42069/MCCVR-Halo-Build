// Worker-only exact native caller proofs. No title image is patched.
// See RENDER-CONTRIBUTIONS-2026-09-23.md and NATIVE-SUBTITLE-H2-H4-2026-09-23.md.
struct NativeSubtitleCallerProof {GameTitle title;uint32_t rva,bytes;uint8_t channel;bool refreshed;const char* pattern;};
constexpr NativeSubtitleCallerProof kNativeSubtitleCallerProofs[]={
    {GameTitle::Halo3,0x599597,0x53,0,false,
        "48 8B 03 48 8D 54 24 50 21 74 24 44 45 33 C9 21 74 24 48 21 74 24 4C 89 74 24 30 F3 0F 11 74 24 28 48 89 4C 24 20 48 8B CB F3 0F 11 44 24 54 F3 0F 11 4C 24 58 C7 44 24 5C 00 00 80 3F C7 44 24 50 00 00 80 3F C7 44 24 40 00 00 80 3F FF 90 C8 02 00 00"},
    {GameTitle::Halo3,0x5BB040,0x57,0,false,
        "48 8B 03 48 8D 54 24 50 44 21 74 24 44 45 33 C9 44 21 74 24 48 44 21 74 24 4C 44 89 74 24 30 F3 0F 11 74 24 28 48 89 4C 24 20 48 8B CB F3 0F 11 44 24 54 F3 0F 11 4C 24 58 C7 44 24 5C 00 00 80 3F C7 44 24 50 00 00 80 3F C7 44 24 40 00 00 80 3F FF 90 C8 02 00 00"},
    {GameTitle::Halo3,0x18B17E,0x2E,0,true,
        "48 8B 01 C7 44 24 30 02 00 00 00 F3 44 0F 11 44 24 28 48 89 74 24 20 4C 8D 8D F8 0D 00 00 4C 8D 44 24 58 48 8D 54 24 48 FF 90 C8 02 00 00"},
    {GameTitle::Halo3ODST,0x5E2B78,0x53,0,false,
        "48 8B 03 48 8D 54 24 50 21 74 24 44 45 33 C9 21 74 24 48 21 74 24 4C 89 74 24 30 F3 0F 11 74 24 28 48 89 4C 24 20 48 8B CB F3 0F 11 44 24 54 F3 0F 11 4C 24 58 C7 44 24 5C 00 00 80 3F C7 44 24 50 00 00 80 3F C7 44 24 40 00 00 80 3F FF 90 C8 02 00 00"},
    {GameTitle::Halo3ODST,0x1BC5C0,0x37,0,true,
        "48 8B 01 F3 41 0F 10 47 28 C7 44 24 30 02 00 00 00 F3 0F 11 44 24 28 48 8D 55 80 48 89 54 24 20 4C 8D 8D 60 14 00 00 4C 8D 44 24 40 48 8D 54 24 50 FF 90 C8 02 00 00"},
    {GameTitle::Halo2,0x6B8232,0x2E,0,false,
        "48 8B 06 48 8D 8D 10 02 00 00 44 89 6C 24 30 4C 8D 45 F0 F3 0F 11 74 24 28 48 8D 55 00 48 89 4C 24 20 45 33 C9 48 8B CE FF 90 C8 02 00 00"},
    {GameTitle::Halo2,0x6F686F,0x35,0,true,
        "48 8B 08 4C 8D 8C 24 A0 00 00 00 C7 44 24 30 02 00 00 00 4C 8D 44 24 50 F3 0F 11 74 24 28 48 8D 54 24 60 48 89 5C 24 20 4C 8B 91 C8 02 00 00 48 8B C8 41 FF D2"},
    {GameTitle::Halo4,0x1301A2,0x38,1,true,
        "48 8B 07 F3 41 0F 10 46 08 44 89 64 24 30 F3 0F 11 44 24 28 48 8D 8D 00 0A 00 00 48 89 4C 24 20 4C 8D 4C 24 60 4C 8D 44 24 78 48 8D 54 24 68 48 8B CF FF 90 C8 02 00 00"},
    {GameTitle::Halo4,0x13021C,0x37,2,true,
        "48 8B 07 F3 0F 10 46 1C 44 89 64 24 30 F3 0F 11 44 24 28 48 8D 8D 00 12 00 00 48 89 4C 24 20 4C 8D 4C 24 60 4C 8D 44 24 78 48 8D 54 24 68 48 8B CF FF 90 C8 02 00 00"},
    {GameTitle::Halo4,0x130267,0x3B,0,true,
        "48 8B 07 F3 41 0F 10 46 08 C7 44 24 30 02 00 00 00 F3 0F 11 44 24 28 48 8D 8D 00 0A 00 00 48 89 4C 24 20 4C 8D 4C 24 60 4C 8D 44 24 78 48 8D 54 24 68 48 8B CF FF 90 C8 02 00 00"},
};
void PollNativeSubtitleSources(bool levelRunning) {
    static uint32_t attempted[7]{};
    const auto title=TitleAdapter_GetActiveTitle();
    for(unsigned i=1;i<7;++i) if(static_cast<GameTitle>(i)!=title||!levelRunning) {
        if(g_subtitleAuthorities[i].generation.load(std::memory_order_relaxed))
            SetSubtitleSources(static_cast<GameTitle>(i),0,nullptr,0);
        attempted[i]=0;
    }
    // Reach source publication additionally requires its existing camera
    // preflight. CE has no proven localized handoff in this candidate.
    const unsigned index=static_cast<unsigned>(title);
    if(!levelRunning||!index||index>=7||title==GameTitle::HaloReach||title==GameTitle::HaloCE) return;
    const uint32_t generation=TitleAdapter_GetGeneration(title);
    if(!generation||attempted[index]==generation) return;
    const wchar_t* module=title==GameTitle::Halo3?L"halo3.dll":
        title==GameTitle::Halo3ODST?L"halo3odst.dll":title==GameTitle::Halo2?L"halo2.dll":L"halo4.dll";
    uintptr_t base=0;size_t size=0;
    if(!sig::ModuleRange(module,base,size)) return;
    attempted[index]=generation;
    SubtitleSource sources[kSubtitleSourceCount]{};size_t count=0,expected=0;
    for(const auto& proof:kNativeSubtitleCallerProofs) if(proof.title==title) {
        ++expected;
        const auto found=Unique(base,size,proof.pattern);
        if(found&&found-base==proof.rva)
            sources[count++]={found+proof.bytes,proof.channel,proof.refreshed};
    }
    // Halo 4's pair/single branches form one bounded caption transaction.
    if(title==GameTitle::Halo4&&count!=expected) count=0;
    if(title!=TitleAdapter_GetActiveTitle()||generation!=TitleAdapter_GetGeneration(title)) return;
    SetSubtitleSources(title,count?generation:0,sources,count);
    LOG("Native subtitles: title=%u generation=%u verified caller sources=%u/%u; missing sources retain stock captions",
        index,generation,static_cast<unsigned>(count),static_cast<unsigned>(expected));
}
