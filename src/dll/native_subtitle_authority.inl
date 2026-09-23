// Bounded atomic source table: the host observer never retains or dereferences
// a title pointer. Worker publication revokes old authority before replacement.
constexpr size_t kSubtitleSourceCount=8;
struct SubtitleSource {
    uintptr_t caller=0;
    uint8_t channel=0; // 0 single caption, 1 first/2 second native queue line
    bool refreshed=false; // source emits each frame with remaining lifetime
};
struct SubtitleAuthority {
    std::atomic<uint64_t> revision{0};
    std::atomic<uint32_t> generation{0};
    std::atomic<uintptr_t> callers[kSubtitleSourceCount]{};
    std::atomic<uint8_t> policies[kSubtitleSourceCount]{};
};
SubtitleAuthority g_subtitleAuthorities[7];
void SetSubtitleSources(GameTitle title,uint32_t generation,const SubtitleSource* sources,size_t count) noexcept {
    const auto index=static_cast<unsigned>(title);
    if(!index||index>=_countof(g_subtitleAuthorities)||count>kSubtitleSourceCount) return;
    auto& authority=g_subtitleAuthorities[index];
    authority.revision.fetch_add(1,std::memory_order_acq_rel);
    authority.generation.store(0,std::memory_order_relaxed);
    for(size_t i=0;i<kSubtitleSourceCount;++i) {
        const SubtitleSource source=sources&&i<count?sources[i]:SubtitleSource{};
        authority.callers[i].store(source.caller,std::memory_order_relaxed);
        authority.policies[i].store(source.channel|(source.refreshed?4u:0u),std::memory_order_relaxed);
    }
    authority.generation.store(generation,std::memory_order_relaxed);
    authority.revision.fetch_add(1,std::memory_order_release);
}
