#include "bloom_override.h"
#include "vr.h"
#include "title_adapter.h"
#include "../common/bloom_override_logic.h"
#include "../common/config.h"
#include "../common/log.h"
#include <array>
#include <vector>

namespace {
using namespace bloom_override;
struct CachedView {
    ID3D11ShaderResourceView* source{};ID3D11ShaderResourceView* zero{};View shape{};
    uint64_t bytes{};
};
struct CachedShader {
    std::atomic<ID3D11PixelShader*> source{};std::atomic<uint32_t> crc{},sequence{};
};
std::array<CachedView,512> views;
std::array<CachedShader,32> shaders;
unsigned viewCount{};
uint64_t cacheBytes{};
ID3D11Device* cachedDevice{}; // retained by the owned context and views
ID3D11DeviceContext* ownedContext{};
std::atomic<ID3D11DeviceContext*> trackedContext{};
std::atomic<ID3D11ShaderResourceView*> boundViews[2]{};
std::atomic<unsigned> knownViews{};
std::atomic<uint32_t> boundShader{};
std::atomic<bool> available{};
BloomSetResourcesFn setResources{};
ReaderGate gate;
SRWLOCK coldLock=SRWLOCK_INIT;
SRWLOCK shaderColdLock=SRWLOCK_INIT;
thread_local bool creatingZero{};
std::atomic<uint64_t> misses{},pressure{},substitutions{},creationFailures{},pendingDrains{};
std::atomic<bool> drainRequested{};

struct ColdWrite {
    bool locked{},admitted{};
    ColdWrite() noexcept {
        locked=TryAcquireSRWLockExclusive(&coldLock)!=FALSE;
        admitted=locked&&gate.Write();
    }
    ~ColdWrite() {if(admitted)gate.EndWrite();if(locked)ReleaseSRWLockExclusive(&coldLock);}
};
unsigned BytesPerPixel(DXGI_FORMAT format) noexcept
{
    // Noncompressed color formats admitted by the contributor's final path.
    switch(format) {
    case DXGI_FORMAT_R32G32B32A32_FLOAT:case DXGI_FORMAT_R32G32B32A32_UINT:case DXGI_FORMAT_R32G32B32A32_SINT:return 16;
    case DXGI_FORMAT_R32G32B32_FLOAT:case DXGI_FORMAT_R32G32B32_UINT:case DXGI_FORMAT_R32G32B32_SINT:return 12;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:case DXGI_FORMAT_R16G16B16A16_FLOAT:case DXGI_FORMAT_R16G16B16A16_UNORM:
    case DXGI_FORMAT_R16G16B16A16_UINT:case DXGI_FORMAT_R16G16B16A16_SNORM:case DXGI_FORMAT_R16G16B16A16_SINT:
    case DXGI_FORMAT_R32G32_TYPELESS:case DXGI_FORMAT_R32G32_FLOAT:case DXGI_FORMAT_R32G32_UINT:case DXGI_FORMAT_R32G32_SINT:return 8;
    case DXGI_FORMAT_R10G10B10A2_UNORM:case DXGI_FORMAT_R10G10B10A2_UINT:case DXGI_FORMAT_R11G11B10_FLOAT:
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:case DXGI_FORMAT_R8G8B8A8_UNORM:case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_R8G8B8A8_UINT:case DXGI_FORMAT_R8G8B8A8_SNORM:case DXGI_FORMAT_R8G8B8A8_SINT:
    case DXGI_FORMAT_B8G8R8A8_UNORM:case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_R16G16_TYPELESS:case DXGI_FORMAT_R16G16_FLOAT:case DXGI_FORMAT_R16G16_UNORM:
    case DXGI_FORMAT_R16G16_UINT:case DXGI_FORMAT_R16G16_SNORM:case DXGI_FORMAT_R16G16_SINT:
    case DXGI_FORMAT_R32_TYPELESS:case DXGI_FORMAT_R32_FLOAT:case DXGI_FORMAT_R32_UINT:case DXGI_FORMAT_R32_SINT:return 4;
    case DXGI_FORMAT_R8G8_TYPELESS:case DXGI_FORMAT_R8G8_UNORM:case DXGI_FORMAT_R8G8_UINT:case DXGI_FORMAT_R8G8_SNORM:case DXGI_FORMAT_R8G8_SINT:
    case DXGI_FORMAT_R16_TYPELESS:case DXGI_FORMAT_R16_FLOAT:case DXGI_FORMAT_R16_UNORM:case DXGI_FORMAT_R16_UINT:case DXGI_FORMAT_R16_SNORM:case DXGI_FORMAT_R16_SINT:return 2;
    case DXGI_FORMAT_R8_TYPELESS:case DXGI_FORMAT_R8_UNORM:case DXGI_FORMAT_R8_UINT:case DXGI_FORMAT_R8_SNORM:case DXGI_FORMAT_R8_SINT:case DXGI_FORMAT_A8_UNORM:return 1;
    default:return 0;
    }
}

void DrainViews() noexcept // cold writer admitted; no draw can restore these
{
    trackedContext.store(nullptr,std::memory_order_release);
    knownViews.store(0,std::memory_order_release);boundShader.store(0,std::memory_order_release);
    for(unsigned i=0;i<viewCount;++i) {
        views[i].zero->Release();views[i].source->Release();views[i]={};
    }
    viewCount=0;cacheBytes=0;
    if(ownedContext){ownedContext->Release();ownedContext=nullptr;}
    cachedDevice=nullptr;
}
bool MakeRoom(uint64_t bytes) noexcept // cold writer owns every reference
{
    while(viewCount==views.size()||bytes>kCacheBudget-cacheBytes) {
        if(!viewCount)return false;
        unsigned largest=0;
        for(unsigned i=1;i<viewCount;++i)if(views[i].bytes>views[largest].bytes)largest=i;
        // Preserve smaller potential pyramids instead of filling the budget
        // permanently with full-resolution scene surfaces. Equal sizes evict
        // the oldest insertion; compaction retains deterministic order.
        if(views[largest].bytes<bytes)return false;
        cacheBytes-=views[largest].bytes;
        views[largest].zero->Release();views[largest].source->Release();
        for(unsigned i=largest+1;i<viewCount;++i)views[i-1]=views[i];
        views[--viewCount]={};
    }
    return true;
}
const CachedView* Find(ID3D11ShaderResourceView* source) noexcept
{
    if(source)for(unsigned i=0;i<viewCount;++i)if(views[i].source==source)return &views[i];
    return nullptr;
}
bool DisabledForTitle(GameTitle title) noexcept
{
    const int index=title==GameTitle::Halo3?0:title==GameTitle::Halo3ODST?1:title==GameTitle::HaloReach?2:-1;
    return index>=0&&!g_config.bloom_enabled[index];
}
}

void Bloom_SetAvailable(bool enabled,BloomSetResourcesFn original) noexcept
{setResources=original;available.store(enabled&&original,std::memory_order_release);}

void Bloom_ObserveSrvCreated(ID3D11Device* device,ID3D11ShaderResourceView* view) noexcept
{
    if(creatingZero||!device||!view||!available.load(std::memory_order_acquire))return;
    D3D11_SHADER_RESOURCE_VIEW_DESC desc{};view->GetDesc(&desc);
    if(desc.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURE2D||desc.Texture2D.MostDetailedMip!=0||!BytesPerPixel(desc.Format))return;
    ID3D11Resource* resource{};view->GetResource(&resource);
    if(!resource)return;
    IDXGIResource* dxgiResource{};DXGI_USAGE usage{};
    const HRESULT dxgi=resource->QueryInterface(__uuidof(IDXGIResource),reinterpret_cast<void**>(&dxgiResource));
    const bool offscreen=SUCCEEDED(dxgi)&&dxgiResource&&SUCCEEDED(dxgiResource->GetUsage(&usage))&&
        !(usage&DXGI_USAGE_BACK_BUFFER);
    if(dxgiResource)dxgiResource->Release();
    if(!offscreen){resource->Release();return;}
    ID3D11Texture2D* texture{};
    const HRESULT qi=resource->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&texture));
    resource->Release();
    if(FAILED(qi)||!texture)return;
    D3D11_TEXTURE2D_DESC td{};texture->GetDesc(&td);texture->Release();
    // Only renderable color pyramids qualify for prewarming. These dimensions
    // never identify a bloom draw; exact title/shader/topology does that later.
    if(td.ArraySize!=1||td.SampleDesc.Count!=1||td.Width<8||td.Height<8||
        !(td.BindFlags&D3D11_BIND_RENDER_TARGET)||!BytesPerPixel(td.Format))return;
    const uint64_t sourceBytes=TextureBytes(td.Width,td.Height,td.MipLevels,BytesPerPixel(td.Format));
    const uint64_t zeroBytes=TextureBytes(td.Width,td.Height,1,BytesPerPixel(td.Format));
    if(!sourceBytes||!zeroBytes||sourceBytes+zeroBytes>kCacheBudget){pressure.fetch_add(1);return;}
    ColdWrite writer;
    if(!writer.admitted){creationFailures.fetch_add(1);return;}
    if(cachedDevice&&cachedDevice!=device)DrainViews();
    if(Find(view))return;
    if(!MakeRoom(sourceBytes+zeroBytes)){pressure.fetch_add(1);return;}
    ID3D11Texture2D* zeroTexture{};ID3D11ShaderResourceView* zeroView{};
    try {
        std::vector<uint8_t> zero(static_cast<size_t>(zeroBytes),0);
        D3D11_TEXTURE2D_DESC zd=td;
        zd.MipLevels=1;zd.Usage=D3D11_USAGE_IMMUTABLE;zd.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        zd.CPUAccessFlags=0;zd.MiscFlags=0;
        D3D11_SUBRESOURCE_DATA data{zero.data(),td.Width*BytesPerPixel(td.Format),0};
        creatingZero=true;
        HRESULT hr=device->CreateTexture2D(&zd,&data,&zeroTexture);
        if(SUCCEEDED(hr)&&zeroTexture) {
            desc.Texture2D.MipLevels=1;
            hr=device->CreateShaderResourceView(zeroTexture,&desc,&zeroView);
        }
        creatingZero=false;
        if(FAILED(hr)||!zeroView) {
            if(zeroTexture)zeroTexture->Release();creationFailures.fetch_add(1);return;
        }
    } catch(...) {
        creatingZero=false;
        if(zeroView)zeroView->Release();if(zeroTexture)zeroTexture->Release();
        creationFailures.fetch_add(1);return;
    }
    zeroTexture->Release(); // zeroView owns its backing allocation
    if(!ownedContext)device->GetImmediateContext(&ownedContext);
    if(!ownedContext){zeroView->Release();creationFailures.fetch_add(1);return;}
    view->AddRef(); // cold lifetime pin: draw restoration never uses borrowed COM ownership
    views[viewCount++]={view,zeroView,{td.Width,td.Height,static_cast<uint32_t>(desc.Format),BytesPerPixel(desc.Format)},
        sourceBytes+zeroBytes};
    cacheBytes+=sourceBytes+zeroBytes; // full pinned source mip chain + single zero mip
    cachedDevice=device;trackedContext.store(ownedContext,std::memory_order_release);
}

void Bloom_ObserveShaderCreated(ID3D11PixelShader* shader,const void* bytes,size_t size) noexcept
{
    if(!shader||!bytes||!size||!available.load(std::memory_order_acquire))return;
    const auto crc=Crc32(bytes,size);
    const uint32_t role=crc==kReachConsumer||crc==kHalo3OdstConsumer?crc:0;
    // Metadata owns no shader reference. Every creation updates a reused
    // address, including unrelated shaders, before it can be bound by MCC.
    // Serialize resource-factory writers only; bind/draw readers never acquire
    // this lock. A sequence rejects a partly published pointer/hash pair.
    AcquireSRWLockExclusive(&shaderColdLock);
    CachedShader* selected=nullptr;
    for(auto& entry:shaders)if(entry.source.load(std::memory_order_acquire)==shader){selected=&entry;break;}
    if(!selected&&role)for(auto& entry:shaders)if(!entry.source.load(std::memory_order_acquire)){selected=&entry;break;}
    if(selected) {
        selected->sequence.fetch_add(1,std::memory_order_acq_rel);
        selected->crc.store(role,std::memory_order_release);
        selected->source.store(role?shader:nullptr,std::memory_order_release);
        selected->sequence.fetch_add(1,std::memory_order_release);
    }
    else if(role)pressure.fetch_add(1);
    ReleaseSRWLockExclusive(&shaderColdLock);
}

void Bloom_ObserveShader(ID3D11DeviceContext* context,ID3D11PixelShader* shader,UINT classCount) noexcept
{
    if(context!=trackedContext.load(std::memory_order_acquire))return;
    uint32_t crc=0;
    if(shader&&!classCount)for(const auto& entry:shaders) {
        const auto before=entry.sequence.load(std::memory_order_acquire);
        if(before&1u)continue;
        if(entry.source.load(std::memory_order_acquire)!=shader)continue;
        const auto candidate=entry.crc.load(std::memory_order_acquire);
        if(entry.sequence.load(std::memory_order_acquire)==before)crc=candidate;
        break;
    }
    boundShader.store(crc,std::memory_order_release);
}
void Bloom_ObserveResources(ID3D11DeviceContext* context,UINT start,UINT count,ID3D11ShaderResourceView* const* input) noexcept
{
    if(context!=trackedContext.load(std::memory_order_acquire)||start>=2||count>128-start)return;
    unsigned known=0;
    for(unsigned i=start;i<2&&i-start<count;++i) {
        boundViews[i].store(input?input[i-start]:nullptr,std::memory_order_release);known|=1u<<i;
    }
    knownViews.fetch_or(known,std::memory_order_release);
}
void Bloom_Invalidate(ID3D11DeviceContext* context,bool shaderToo) noexcept
{
    if(context!=trackedContext.load(std::memory_order_acquire))return;
    knownViews.store(0,std::memory_order_release);
    if(shaderToo)boundShader.store(0,std::memory_order_release);
}

BloomDrawScope::BloomDrawScope(ID3D11DeviceContext* value) noexcept
{
    if(!available.load(std::memory_order_acquire)||!DisabledForTitle(TitleAdapter_GetActiveTitle())||
        !VR_IsEyeRasterActive(value)||value!=trackedContext.load(std::memory_order_acquire))return;
    const uint32_t crc=boundShader.load(std::memory_order_acquire);
    if(crc!=kReachConsumer&&crc!=kHalo3OdstConsumer)return;
    if(!gate.Enter())return;
    reading=true;
    if(value!=trackedContext.load(std::memory_order_acquire))return;
    const unsigned known=knownViews.load(std::memory_order_acquire);
    const CachedView* slots[2]{Find(boundViews[0].load(std::memory_order_acquire)),Find(boundViews[1].load(std::memory_order_acquire))};
    mask=Slots(TitleAdapter_GetActiveTitle(),crc,slots[0]?slots[0]->shape:View{},slots[1]?slots[1]->shape:View{});
    if(!mask||(known&mask)!=mask){misses.fetch_add(1,std::memory_order_relaxed);mask=0;return;}
    context=value;
    for(unsigned i=0;i<2;++i)if(mask&(1u<<i))original[i]=slots[i]->source;
    if(mask==3) {
        ID3D11ShaderResourceView* replacements[2]{slots[0]->zero,slots[1]->zero};
        setResources(context,0,2,replacements);
    } else {
        ID3D11ShaderResourceView* replacement=slots[1]->zero;
        setResources(context,1,1,&replacement);
    }
    substitutions.fetch_add(1,std::memory_order_relaxed);
}
BloomDrawScope::~BloomDrawScope()
{
    if(mask==3)setResources(context,0,2,original);
    else if(mask==2)setResources(context,1,1,&original[1]);
    if(reading)gate.Leave();
}

void Bloom_ColdTick(GameTitle title,uint32_t generation) noexcept
{
    static GameTitle previous=GameTitle::None;
    static uint32_t previousGeneration{};
    if(previous!=title||previousGeneration!=generation) {
        // Creation may precede title discovery, and resources may survive a
        // warm return. Pointer ownership, not an inferred title tag, is their
        // lifetime proof. Keep the bounded pool; revoke only cached bindings.
        Bloom_Invalidate(trackedContext.load(std::memory_order_acquire));
        previous=title;previousGeneration=generation;
    }
    if(drainRequested.load(std::memory_order_acquire)) {
        ColdWrite writer;
        if(writer.admitted){DrainViews();drainRequested.store(false,std::memory_order_release);}
        else pendingDrains.fetch_add(1,std::memory_order_relaxed);
    }
    static uint64_t last{},priorPressure{},priorFailures{};
    const auto now=GetTickCount64();
    if(now>=last&&now-last<5000)return;
    last=now;
    const auto missesNow=misses.exchange(0),draws=substitutions.exchange(0),p=pressure.load(),f=creationFailures.load();
    if(DisabledForTitle(title)&&(missesNow||draws||p!=priorPressure||f!=priorFailures||drainRequested.load()))
        LOG("Bloom consumer: title=%u substituted=%llu stock-misses=%llu capacity=%llu creation=%llu drainPending=%d; "
            "exact consumer only, unavailable views remain native; camera/OpenXR unaffected",
            unsigned(title),(unsigned long long)draws,(unsigned long long)missesNow,
            (unsigned long long)p,(unsigned long long)f,drainRequested.load()?1:0);
    priorPressure=p;priorFailures=f;
}

void Bloom_BeforeResize() noexcept
{
    drainRequested.store(true,std::memory_order_release);
    ColdWrite writer;
    if(writer.admitted){DrainViews();drainRequested.store(false,std::memory_order_release);}
}
