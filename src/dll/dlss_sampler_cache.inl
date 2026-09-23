// Sampler variants are created only beside the game's CreateSamplerState call.
// Bind/eye hooks perform bounded immutable lookup: no COM queries, allocation,
// logging, locks or cache teardown. Source references prevent pointer recycling.
// This bounded process-lifetime registry matches the process-lifetime D3D hooks.
namespace
{
struct SamplerTwin
{
    std::atomic<uintptr_t> source{0}; // 0 empty, 1 reserved, otherwise retained source
    ID3D11SamplerState* variants[dlss_sampler::kModes]{};
};
constexpr unsigned kSamplerTwinSlots=256,kSamplerTwinProbe=4;
SamplerTwin g_samplerTwins[kSamplerTwinSlots]{};
std::atomic<unsigned> g_samplerMode{UINT_MAX};
std::atomic<DWORD> g_samplerBiasThread{0};
std::atomic<uint64_t> g_samplerTwinsMade{0},g_samplerMisses{0},g_samplerFailures{0};

ID3D11SamplerState* BiasedSamplerTwin(ID3D11SamplerState* source,unsigned mode) noexcept
{
    if(!source||mode>=dlss_sampler::kModes) return source;
    const auto key=reinterpret_cast<uintptr_t>(source);
    const size_t home=(key>>4)&(kSamplerTwinSlots-1);
    for(unsigned i=0;i<kSamplerTwinProbe;++i)
    {
        const auto& entry=g_samplerTwins[(home+i)&(kSamplerTwinSlots-1)];
        if(entry.source.load(std::memory_order_acquire)==key)
            return entry.variants[mode]?entry.variants[mode]:source;
    }
    g_samplerMisses.fetch_add(1,std::memory_order_relaxed);
    return source;
}
}

static HRESULT STDMETHODCALLTYPE CreateSamplerStateHook(ID3D11Device* device,
    const D3D11_SAMPLER_DESC* descriptor,ID3D11SamplerState** output)
{
    const HRESULT result=g_origCreateSamplerState(device,descriptor,output);
    if(FAILED(result)||!descriptor||!output||!*output) return result;
    const auto key=reinterpret_cast<uintptr_t>(*output);
    const size_t home=(key>>4)&(kSamplerTwinSlots-1);
    SamplerTwin* slot=nullptr;
    for(unsigned i=0;i<kSamplerTwinProbe;++i)
    {
        auto& candidate=g_samplerTwins[(home+i)&(kSamplerTwinSlots-1)];
        uintptr_t value=candidate.source.load(std::memory_order_acquire);
        if(value==key) return result;
        if(!value&&candidate.source.compare_exchange_strong(value,1,std::memory_order_acq_rel))
        {slot=&candidate;break;}
    }
    if(!slot) {g_samplerFailures.fetch_add(1,std::memory_order_relaxed);return result;}
    // Call the original factory directly, so a variant never recursively
    // produces more variants. Failure leaves that mode's original sampler.
    (*output)->AddRef();
    for(unsigned mode=0;mode<dlss_sampler::kModes;++mode)
    {
        D3D11_SAMPLER_DESC variant=*descriptor;
        variant.MipLODBias=dlss_sampler::Adjust(descriptor->MipLODBias,mode);
        if(FAILED(g_origCreateSamplerState(device,&variant,&slot->variants[mode])))
        {slot->variants[mode]=nullptr;g_samplerFailures.fetch_add(1,std::memory_order_relaxed);}
    }
    slot->source.store(key,std::memory_order_release);
    g_samplerTwinsMade.fetch_add(1,std::memory_order_relaxed);
    return result;
}

static void STDMETHODCALLTYPE PSSetSamplersHook(ID3D11DeviceContext* context,UINT start,
    UINT count,ID3D11SamplerState* const* samplers)
{
    const unsigned mode=g_samplerMode.load(std::memory_order_acquire);
    if(mode>=dlss_sampler::kModes||!samplers||!count||
       start>=D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT||
       count>D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT-start||
       g_samplerBiasThread.load(std::memory_order_relaxed)!=GetCurrentThreadId())
    {g_origPSSetSamplers(context,start,count,samplers);return;}
    ID3D11SamplerState* replacements[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT]{};
    for(UINT i=0;i<count;++i) replacements[i]=BiasedSamplerTwin(samplers[i],mode);
    g_origPSSetSamplers(context,start,count,replacements);
}

void D3D_SetEyeSamplerBias(float bias)
{
    const int mode=g_origCreateSamplerState&&g_origPSSetSamplers?dlss_sampler::Mode(bias):-1;
    g_samplerMode.store(UINT_MAX,std::memory_order_release);
    if(mode<0) return;
    g_samplerBiasThread.store(GetCurrentThreadId(),std::memory_order_relaxed);
    g_samplerMode.store(static_cast<unsigned>(mode),std::memory_order_release);
}
