#include "haloce_eye_cache.h"
#include <cmath>
#include <limits>

namespace halo_ce
{
namespace
{
bool Supported(const D3D11_TEXTURE2D_DESC& d) noexcept
{
    // Color-only, uncompressed, ordinary single-sample 2D primary output.
    // Depth/MSAA/arrays require separately verified resolve/routing paths.
    const bool color=d.Format==DXGI_FORMAT_R8G8B8A8_UNORM||
        d.Format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB||
        d.Format==DXGI_FORMAT_R8G8B8A8_TYPELESS||
        d.Format==DXGI_FORMAT_B8G8R8A8_UNORM||
        d.Format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB||
        d.Format==DXGI_FORMAT_B8G8R8A8_TYPELESS||
        d.Format==DXGI_FORMAT_R16G16B16A16_FLOAT||
        d.Format==DXGI_FORMAT_R16G16B16A16_TYPELESS||
        d.Format==DXGI_FORMAT_R10G10B10A2_UNORM;
    return color&&d.Width&&d.Width<=16384&&d.Height&&d.Height<=16384&&
        d.MipLevels==1&&d.ArraySize==1&&d.SampleDesc.Count==1&&
        d.SampleDesc.Quality==0&&d.Usage==D3D11_USAGE_DEFAULT&&
        !d.CPUAccessFlags&&!(d.BindFlags&D3D11_BIND_DEPTH_STENCIL)&&!d.MiscFlags;
}
bool SameSource(const D3D11_TEXTURE2D_DESC& a,const D3D11_TEXTURE2D_DESC& b) noexcept
{
    return a.Width==b.Width&&a.Height==b.Height&&a.MipLevels==b.MipLevels&&
        a.ArraySize==b.ArraySize&&a.Format==b.Format&&
        a.SampleDesc.Count==b.SampleDesc.Count&&a.SampleDesc.Quality==b.SampleDesc.Quality&&
        a.Usage==b.Usage&&a.BindFlags==b.BindFlags&&
        a.CPUAccessFlags==b.CPUAccessFlags&&a.MiscFlags==b.MiscFlags;
}
DXGI_FORMAT CopyFormatFamily(DXGI_FORMAT format) noexcept
{
    switch (format)
    {
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        return DXGI_FORMAT_R8G8B8A8_TYPELESS;
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        return DXGI_FORMAT_B8G8R8A8_TYPELESS;
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
        return DXGI_FORMAT_R16G16B16A16_TYPELESS;
    default:return format;
    }
}
}
bool EyeCache::Enter() noexcept
{
    uint64_t expected=0;
    return use_.compare_exchange_strong(expected,1,std::memory_order_acquire);
}
void EyeCache::Leave() noexcept { use_.store(0,std::memory_order_release); }
void EyeCache::ClearFrame() noexcept { key_={}; mask_=0; depthMask_=0; complete_=false; }
void EyeCache::ReleaseResources() noexcept
{
    ClearFrame();
    ReleaseDepthResources();
    completedKey_={}; completedTracking_={};
    for (auto*& eye:eyes_) { if (eye) eye->Release(); eye=nullptr; }
    for (auto*& eye:completedEyes_) { if (eye) eye->Release(); eye=nullptr; }
    if (context_) context_->Release();
    context_=nullptr; source_={}; cache_={}; generation_=0; resourceEpoch_=0; lastSerial_=0;
}
EyeCache::~EyeCache() { (void)Reset(); }
bool EyeCache::Reset() noexcept
{
    if (!Enter()) return false;
    ReleaseResources(); Leave(); return true;
}
bool EyeCache::Prepare(ID3D11Device* device,ID3D11DeviceContext* context,
    const D3D11_TEXTURE2D_DESC& source,uint32_t generation,uint64_t resourceEpoch) noexcept
{
    if (!Enter()) return false;
    // Even an unsuccessful replacement must revoke yesterday's completed pair.
    ClearFrame(); completedKey_={};
    bool valid=device&&context&&generation&&resourceEpoch>lastResourceEpoch_&&Supported(source);
    ID3D11Device* contextDevice=nullptr;
    if (valid)
    {
        context->GetDevice(&contextDevice);
        valid=contextDevice==device&&context->GetType()==D3D11_DEVICE_CONTEXT_IMMEDIATE;
        if (contextDevice) contextDevice->Release();
    }
    ID3D11Texture2D* prepared[4]{};
    D3D11_TEXTURE2D_DESC descriptor=source;
    descriptor.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    if (valid)
        valid=SUCCEEDED(device->CreateTexture2D(&descriptor,nullptr,&prepared[0]))&&
            SUCCEEDED(device->CreateTexture2D(&descriptor,nullptr,&prepared[1]))&&
            SUCCEEDED(device->CreateTexture2D(&descriptor,nullptr,&prepared[2]))&&
            SUCCEEDED(device->CreateTexture2D(&descriptor,nullptr,&prepared[3]));
    ReleaseResources();
    if (valid)
    {
        context->AddRef(); context_=context;
        eyes_[0]=prepared[0]; eyes_[1]=prepared[1];
        completedEyes_[0]=prepared[2]; completedEyes_[1]=prepared[3];
        source_=source; cache_=descriptor;
        generation_=generation; resourceEpoch_=resourceEpoch; lastResourceEpoch_=resourceEpoch;
    }
    else for (auto* eye:prepared) if (eye) eye->Release();
    Leave(); return valid;
}
bool EyeCache::Begin(const PreparedReceipt& receipt,Key& key) noexcept
{
    if (!Enter()) return false;
    ClearFrame();
    const auto& t=receipt.tracking;
    const auto& p=receipt.pair;
    bool valid=context_&&eyes_[0]&&eyes_[1]&&t.serial&&t.serial>lastSerial_&&
        receipt.ticket.revision&&receipt.ticket.sourceList&&
        receipt.ticket.generation==generation_&&t.generation==generation_&&
        p.generation==generation_&&p.serial==t.serial&&p.spaceEpoch==t.spaceEpoch;
    for (int eye=0;eye<2&&valid;++eye)
    {
        const auto& cover=p.covers[eye];
        valid=p.cameras[eye].viewportWidth==source_.Width&&
            p.cameras[eye].viewportHeight==source_.Height&&
            std::isfinite(cover.halfX)&&cover.halfX>0&&cover.halfX<1.55f&&
            std::isfinite(cover.halfY)&&cover.halfY>0&&cover.halfY<1.55f;
    }
    if (t.generation==generation_&&t.serial>lastSerial_) lastSerial_=t.serial;
    if (valid)
    {
        key_={generation_,t.spaceEpoch,t.serial,resourceEpoch_};
        tracking_=t; covers_[0]=p.covers[0]; covers_[1]=p.covers[1]; key=key_;
    }
    Leave(); return valid;
}
bool EyeCache::Begin(const ClassicViewPair& pair,Key& key) noexcept
{
    if (!Enter()) return false;
    ClearFrame();
    const auto& t=pair.tracking;
    bool valid=context_&&eyes_[0]&&eyes_[1]&&t.serial&&t.serial>lastSerial_&&
        t.generation==generation_&&t.spaceEpoch&&pair.rendererEpoch&&ValidPrimary(pair.source)&&
        std::isfinite(pair.cover.halfX)&&pair.cover.halfX>0&&pair.cover.halfX<1.55f&&
        std::isfinite(pair.cover.halfY)&&pair.cover.halfY>0&&pair.cover.halfY<1.55f;
    for (int eye=0;eye<2&&valid;++eye)
    {
        const auto& w=pair.eyes[eye];
        valid=ClassicFullRaster(w);
    }
    if (t.generation==generation_&&t.serial>lastSerial_) lastSerial_=t.serial;
    if (valid)
    {
        key_={generation_,t.spaceEpoch,t.serial,resourceEpoch_};
        tracking_=t; covers_[0]=covers_[1]=pair.cover; key=key_;
    }
    Leave(); return valid;
}
bool EyeCache::Capture(Key key,int eye,ID3D11DeviceContext* context,
    ID3D11Resource* liveSource,const D3D11_TEXTURE2D_DESC& provenSource) noexcept
{
    if (!Enter()) return false;
    const bool valid=key.serial&&key==key_&&eye>=0&&eye<2&&!complete_&&
        mask_==(eye==0?0u:1u)&&context&&context==context_&&liveSource&&
        liveSource!=eyes_[0]&&liveSource!=eyes_[1]&&
        liveSource!=completedEyes_[0]&&liveSource!=completedEyes_[1]&&SameSource(source_,provenSource);
    if (valid)
    {
        const D3D11_BOX box{0,0,0,source_.Width,source_.Height,1};
        context->CopySubresourceRegion(eyes_[eye],0,0,0,0,liveSource,0,&box);
        mask_|=1u<<eye;
    }
    else ClearFrame();
    Leave(); return valid;
}
bool EyeCache::CapturePacked(Key key,ID3D11DeviceContext* context,
    ID3D11Resource* liveSource,const D3D11_TEXTURE2D_DESC& provenPackedSource) noexcept
{
    if (!Enter()) return false;
    const bool valid=key.serial&&key==key_&&mask_==3&&!complete_&&
        context&&context==context_&&eyes_[0]&&eyes_[1]&&eyes_[0]!=eyes_[1]&&
        liveSource&&liveSource!=eyes_[0]&&liveSource!=eyes_[1]&&
        liveSource!=completedEyes_[0]&&liveSource!=completedEyes_[1]&&
        Supported(provenPackedSource)&&provenPackedSource.Width==source_.Width&&
        provenPackedSource.Height==2*source_.Height&&
        CopyFormatFamily(provenPackedSource.Format)==CopyFormatFamily(source_.Format);
    if (valid)
    {
        // Check the whole transaction before either copy. No partial HUD
        // replacement can occur because one eye failed a later shape guard.
        const D3D11_BOX left{0,0,0,source_.Width,source_.Height,1};
        const D3D11_BOX right{0,source_.Height,0,source_.Width,2*source_.Height,1};
        context->CopySubresourceRegion(eyes_[0],0,0,0,0,liveSource,0,&left);
        context->CopySubresourceRegion(eyes_[1],0,0,0,0,liveSource,0,&right);
    }
    // HUD is optional: a refused late source must not invalidate world stereo.
    Leave();return valid;
}
bool EyeCache::Finish(Key key) noexcept
{
    if (!Enter()) return false;
    const bool valid=key.serial&&key==key_&&mask_==3&&!complete_;
    if (valid)
    {
        // Publish pixels and their exact tracking identity together. Both
        // banks were allocated cold; no COM ownership changes occur here.
        for (int eye=0;eye<2;++eye)
        {
            auto* previous=completedEyes_[eye];
            completedEyes_[eye]=eyes_[eye];eyes_[eye]=previous;
            completedCovers_[eye]=covers_[eye];
        }
        completedKey_=key_;completedTracking_=tracking_;complete_=true;
        completedDepthValid_=depthMask_==3;
        completedDepthHistoryEpoch_=completedDepthValid_?depthHistoryEpoch_:0;
        if(completedDepthValid_) for(int eye=0;eye<2;++eye) {
            std::swap(depth_[eye],completedDepth_[eye]);
            std::swap(depthViews_[eye],completedDepthViews_[eye]);
            completedDepthCameras_[eye]=depthCameras_[eye];
        }
    }
    else ClearFrame();
    Leave(); return valid;
}
bool EyeCache::Drop(Key key) noexcept
{
    if (!Enter()) return false;
    if (key==key_) ClearFrame();
    Leave(); return true;
}
bool EyeCache::AcquireCompleted(Key key,ID3D11DeviceContext* submissionContext,Completed& out) noexcept
{
    if (!Enter()) return false;
    if (!key.serial||key!=completedKey_||submissionContext!=context_||
        lastBorrowId_==std::numeric_limits<uint64_t>::max())
    { Leave(); return false; }
    const uint64_t borrow=++lastBorrowId_;
    out={completedKey_,completedTracking_,{completedCovers_[0],completedCovers_[1]},
        {completedEyes_[0],completedEyes_[1]},cache_,borrow};
    if(completedDepthValid_) {
        out.depthDescriptor=depthDescriptor_;
        out.depthHistoryEpoch=completedDepthHistoryEpoch_;
        for(int eye=0;eye<2;++eye) {
            out.depthViews[eye]=completedDepthViews_[eye];
            out.depthCameras[eye]=completedDepthCameras_[eye];
        }
    }
    use_.store(borrow,std::memory_order_release);
    return true;
}
bool EyeCache::ReleaseCompleted(uint64_t borrowId) noexcept
{
    // A delayed/duplicate release cannot retire a newer submission's borrow.
    if (borrowId<2||!use_.compare_exchange_strong(borrowId,1,std::memory_order_acquire))
        return false;
    // A later rejected native frame may re-submit this same coherent pair.
    // The adapter bounds reuse by its generation, reference, epoch and age.
    Leave(); return true;
}
#include "haloce_eye_depth_cache.inl"
}
