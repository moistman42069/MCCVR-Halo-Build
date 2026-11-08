#include "haloce_eye_cache.h"
#include <cstdio>
#include <vector>
#include <thread>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;
using namespace halo_ce;
namespace
{
PreparedReceipt Receipt(uint64_t serial,uint32_t width,uint32_t height)
{
    PreparedReceipt r{};
    r.ticket={1,0x12340,3,PreparationOrigin::CopiedList};
    r.tracking.serial=r.pair.serial=serial;
    r.tracking.generation=r.pair.generation=3;
    r.tracking.spaceEpoch=r.pair.spaceEpoch=7;
    r.tracking.headPosition={0.1f,1.6f,0.2f};
    for (int eye=0;eye<2;++eye)
    {
        r.pair.cameras[eye].viewportWidth=static_cast<float>(width);
        r.pair.cameras[eye].viewportHeight=static_cast<float>(height);
        r.pair.covers[eye]={1.8f,1.0f,0.9f};
    }
    return r;
}
bool Pixels(ID3D11Device* device,ID3D11DeviceContext* context,
    ID3D11Texture2D* texture,const D3D11_TEXTURE2D_DESC& source,uint32_t expected)
{
    auto d=source;
    d.BindFlags=0; d.Usage=D3D11_USAGE_STAGING; d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> readback;
    if (FAILED(device->CreateTexture2D(&d,nullptr,&readback))) return false;
    context->CopyResource(readback.Get(),texture);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped))) return false;
    bool ok=true;
    for (UINT y=0;y<d.Height;++y)
    {
        const auto* row=reinterpret_cast<const uint32_t*>(
            static_cast<const uint8_t*>(mapped.pData)+y*mapped.RowPitch);
        for (UINT x=0;x<d.Width;++x) if (row[x]!=expected) ok=false;
    }
    context->Unmap(readback.Get(),0);
    return ok;
}
}
int main()
{
    int failures=0;
    const auto check=[&](bool ok,const char* why) {
        if (!ok) { ++failures; std::fprintf(stderr,"CE GPU cache: %s\n",why); }
    };
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    const D3D_FEATURE_LEVEL level=D3D_FEATURE_LEVEL_11_0;
    const HRESULT hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,
        &level,1,D3D11_SDK_VERSION,&device,nullptr,&context);
    if (FAILED(hr)) { std::fprintf(stderr,"WARP device failed: %08lx\n",hr); return 1; }
    D3D11_TEXTURE2D_DESC d{};
    d.Width=32; d.Height=16; d.MipLevels=1; d.ArraySize=1;
    d.Format=DXGI_FORMAT_R8G8B8A8_UNORM; d.SampleDesc.Count=1;
    d.Usage=D3D11_USAGE_DEFAULT; d.BindFlags=D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> source;
    if (FAILED(device->CreateTexture2D(&d,nullptr,&source))) return 1;
    std::vector<uint32_t> pixels(d.Width*d.Height);
    const auto paint=[&](uint32_t color) {
        std::fill(pixels.begin(),pixels.end(),color);
        context->UpdateSubresource(source.Get(),0,nullptr,pixels.data(),d.Width*4,0);
    };
    EyeCache cache;
    EyeCache::Key key{};
    EyeCache::Completed complete{};
    check(cache.Prepare(device.Get(),context.Get(),d,3,1),"cold cache preparation");
    check(cache.Begin(Receipt(100,d.Width,d.Height),key),"receipt starts an exact frame");
    check(!cache.AcquireCompleted(key,context.Get(),complete),"no unrendered submission");
    paint(0xff112233);
    check(cache.Capture(key,0,context.Get(),source.Get(),d),"capture left from native-lived source");
    check(!cache.AcquireCompleted(key,context.Get(),complete),"no half-pair submission");
    paint(0xffaabbcc);
    check(cache.Capture(key,1,context.Get(),source.Get(),d),"capture right from reused native surface");
    check(!cache.AcquireCompleted(key,context.Get(),complete),"native frame must finish first");
    paint(0xff000000);
    source.Reset();
    check(cache.Finish(key),"completed native frame publishes pair");
    bool borrowed=cache.AcquireCompleted(key,context.Get(),complete);
    const uint64_t firstBorrow=complete.borrowId;
    check(borrowed,"complete pair is available");
    if (borrowed)
    {
        check(complete.tracking.serial==100&&complete.tracking.spaceEpoch==7&&
            complete.tracking.headPosition.y==1.6f,"submission retains preparation tracking");
        check(Pixels(device.Get(),context.Get(),complete.eyes[0],complete.descriptor,0xff112233)&&
            Pixels(device.Get(),context.Get(),complete.eyes[1],complete.descriptor,0xffaabbcc),
            "real GPU copies preserve distinct eyes after native surface recycling and release");
        std::thread cold([&] {
            check(!cache.Reset(),"cold retirement cannot release borrowed textures");
            check(!cache.Prepare(device.Get(),context.Get(),d,3,2),"cold replacement cannot race submission");
        });
        cold.join();
        check(Pixels(device.Get(),context.Get(),complete.eyes[0],complete.descriptor,0xff112233),
            "declined cold replacement leaves borrowed GPU storage intact");
        check(cache.ReleaseCompleted(complete.borrowId),"release exact submission borrow");
    }
    if (FAILED(device->CreateTexture2D(&d,nullptr,&source))) return 1;
    const auto retainedKey=key;
    const auto retainedPairIntact=[&] {
        EyeCache::Completed retained{};
        if (!cache.AcquireCompleted(retainedKey,context.Get(),retained)) return false;
        const bool intact=retained.key==retainedKey&&retained.tracking.serial==100&&
            retained.tracking.headPosition.y==1.6f&&
            Pixels(device.Get(),context.Get(),retained.eyes[0],retained.descriptor,0xff112233)&&
            Pixels(device.Get(),context.Get(),retained.eyes[1],retained.descriptor,0xffaabbcc);
        return cache.ReleaseCompleted(retained.borrowId)&&intact;
    };
    check(retainedPairIntact(),"last complete pair may be re-submitted with its original tracking");
    check(!cache.Begin(Receipt(100,d.Width,d.Height),key),"same tracking frame cannot be rendered twice");
    check(retainedPairIntact(),"duplicate preparation does not erase the complete pair");
    check(cache.Begin(Receipt(101,d.Width,d.Height),key),"next frame recovers");
    check(!cache.Capture(key,1,context.Get(),source.Get(),d)&&!cache.Finish(key),
        "out-of-order eye drops only its frame");
    check(cache.Begin(Receipt(102,d.Width,d.Height),key),"recover after order failure");
    paint(0xff778899);
    check(cache.Capture(key,0,context.Get(),source.Get(),d)&&
        !cache.Capture(key,0,context.Get(),source.Get(),d)&&!cache.Finish(key),"duplicate eye invalidates pair");
    check(retainedPairIntact(),"a partially copied or rejected successor never changes either retained eye");
    check(cache.Begin(Receipt(103,d.Width,d.Height),key),"start descriptor mismatch case");
    auto changed=d; ++changed.Width;
    check(!cache.Capture(key,0,context.Get(),source.Get(),changed)&&!cache.Finish(key),
        "resize cannot copy using a stale descriptor");
    ComPtr<ID3D11DeviceContext> deferred;
    check(SUCCEEDED(device->CreateDeferredContext(0,&deferred)),"create independent deferred context");
    check(cache.Begin(Receipt(104,d.Width,d.Height),key)&&
        !cache.Capture(key,0,deferred.Get(),source.Get(),d),"wrong/deferred context cannot capture");
    check(cache.Begin(Receipt(105,d.Width,d.Height),key)&&cache.Drop(key)&&!cache.Finish(key),
        "native frame failure drops captured identity");
    check(retainedPairIntact(),"all rejected successors preserve complete pixels and original poses");
    const auto oldKey=key;
    check(cache.Reset()&&cache.Prepare(device.Get(),context.Get(),d,3,2),"cold retirement and rebuild");
    check(!cache.AcquireCompleted(retainedKey,context.Get(),complete),
        "resource reset revokes retained pixels, even if tracking identity repeats");
    check(cache.Begin(Receipt(105,d.Width,d.Height),key)&&key.resourceEpoch!=oldKey.resourceEpoch,
        "new resource epoch prevents stale GPU identity reuse");
    check(!cache.Capture(oldKey,0,context.Get(),source.Get(),d),"old cache callback rejected");
    check(cache.Begin(Receipt(106,d.Width,d.Height),key)&&
        cache.Capture(key,0,context.Get(),source.Get(),d)&&
        cache.Capture(key,1,context.Get(),source.Get(),d)&&cache.Finish(key),"recover after stale callback");
    check(!cache.AcquireCompleted(retainedKey,context.Get(),complete),
        "new complete pair replaces the previous pair as a whole");
    check(!cache.AcquireCompleted(key,deferred.Get(),complete),"submission must preserve context ordering");
    borrowed=cache.AcquireCompleted(key,context.Get(),complete);
    check(borrowed,"incorrect context does not consume a valid pair");
    if (borrowed)
    {
        check(!cache.ReleaseCompleted(firstBorrow)&&!cache.Reset(),
            "delayed old borrow release cannot free a newer submission");
        check(cache.ReleaseCompleted(complete.borrowId),"new borrow remains releasable");
    }
    {
        auto packedDescriptor=d;packedDescriptor.Height*=2;
        ComPtr<ID3D11Texture2D> packed;
        check(SUCCEEDED(device->CreateTexture2D(&packedDescriptor,nullptr,&packed)),
            "create real native packed output surface");
        std::vector<uint32_t> packedPixels(packedDescriptor.Width*packedDescriptor.Height);
        const auto paintPacked=[&](uint32_t left,uint32_t right) {
            std::fill(packedPixels.begin(),packedPixels.begin()+pixels.size(),left);
            std::fill(packedPixels.begin()+pixels.size(),packedPixels.end(),right);
            context->UpdateSubresource(packed.Get(),0,nullptr,packedPixels.data(),packedDescriptor.Width*4,0);
        };
        const auto captureWorld=[&](uint64_t serial) {
            bool ok=cache.Begin(Receipt(serial,d.Width,d.Height),key);
            paint(0xff102030);ok=cache.Capture(key,0,context.Get(),source.Get(),d)&&ok;
            paint(0xff405060);return cache.Capture(key,1,context.Get(),source.Get(),d)&&ok;
        };
        const auto worldIntact=[&] {
            if (!cache.Finish(key)||!cache.AcquireCompleted(key,context.Get(),complete)) return false;
            const bool ok=Pixels(device.Get(),context.Get(),complete.eyes[0],complete.descriptor,0xff102030)&&
                Pixels(device.Get(),context.Get(),complete.eyes[1],complete.descriptor,0xff405060);
            return cache.ReleaseCompleted(complete.borrowId)&&ok;
        };
        paintPacked(0xffa1b2c3,0xffd4e5f6);
        check(cache.Begin(Receipt(200,d.Width,d.Height),key),"start incomplete late-HUD case");
        paint(0xff102030);
        check(cache.Capture(key,0,context.Get(),source.Get(),d),"capture first world eye before optional HUD");
        check(!cache.CapturePacked(key,context.Get(),packed.Get(),packedDescriptor),
            "late HUD cannot replace an incomplete world pair");
        paint(0xff405060);
        check(cache.Capture(key,1,context.Get(),source.Get(),d)&&worldIntact(),
            "premature HUD refusal preserves first eye and permits normal world completion");

        check(captureWorld(201),"capture world pair for rejected packed sources");
        for (int field=0;field<4;++field)
        {
            auto stale=key;
            switch (field)
            {
            case 0:++stale.generation;break;case 1:++stale.spaceEpoch;break;
            case 2:++stale.serial;break;case 3:++stale.resourceEpoch;break;
            }
            check(!cache.CapturePacked(stale,context.Get(),packed.Get(),packedDescriptor),
                "each stale packed receipt identity component is rejected");
        }
        for (int field=0;field<12;++field)
        {
            auto rejected=packedDescriptor;
            switch (field)
            {
            case 0:++rejected.Width;break;case 1:--rejected.Height;break;
            case 2:rejected.Height=32768;break;case 3:rejected.MipLevels=2;break;
            case 4:rejected.ArraySize=2;break;case 5:rejected.SampleDesc.Count=2;break;
            case 6:rejected.SampleDesc.Quality=1;break;case 7:rejected.Format=DXGI_FORMAT_B8G8R8A8_UNORM;break;
            case 8:rejected.Usage=D3D11_USAGE_STAGING;break;case 9:rejected.CPUAccessFlags=D3D11_CPU_ACCESS_READ;break;
            case 10:rejected.BindFlags|=D3D11_BIND_DEPTH_STENCIL;break;case 11:rejected.MiscFlags=1;break;
            }
            check(!cache.CapturePacked(key,context.Get(),packed.Get(),rejected),
                "incompatible packed layout is refused before either eye copy");
        }
        check(!cache.CapturePacked(key,deferred.Get(),packed.Get(),packedDescriptor)&&
            !cache.CapturePacked(key,nullptr,packed.Get(),packedDescriptor)&&
            !cache.CapturePacked(key,context.Get(),nullptr,packedDescriptor)&&
            !cache.CapturePacked(key,context.Get(),complete.eyes[0],packedDescriptor)&&
            !cache.CapturePacked(key,context.Get(),complete.eyes[1],packedDescriptor),
            "wrong context, absent source and aliases of either eye are rejected");
        check(cache.Finish(key),"refused HUD operations retain a publishable world pair");
        check(!cache.CapturePacked(key,context.Get(),packed.Get(),packedDescriptor),
            "finished frame cannot accept late replacement");
        borrowed=cache.AcquireCompleted(key,context.Get(),complete);
        check(borrowed,"late replacement refusal leaves completed world pair available");
        if (borrowed)
        {
            check(!cache.CapturePacked(key,context.Get(),packed.Get(),packedDescriptor),
                "submission borrow excludes optional packed writes");
            check(Pixels(device.Get(),context.Get(),complete.eyes[0],complete.descriptor,0xff102030)&&
                Pixels(device.Get(),context.Get(),complete.eyes[1],complete.descriptor,0xff405060),
                "all rejected optional replacements leave both world eye pixels intact");
            check(cache.ReleaseCompleted(complete.borrowId),"release unchanged world pair");
        }
        uint64_t serial=202;
        for (const auto format:{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
            DXGI_FORMAT_R8G8B8A8_TYPELESS})
        {
            packed.Reset();packedDescriptor.Format=format;
            packedDescriptor.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
            check(SUCCEEDED(device->CreateTexture2D(&packedDescriptor,nullptr,&packed)),
                "create actual compatible native packed format");
            paintPacked(0xffa1b2c3,0xffd4e5f6);
            check(captureWorld(serial++),"capture world pair before native late-HUD finish");
            check(cache.CapturePacked(key,context.Get(),packed.Get(),packedDescriptor),
                "capture both packed native output halves with compatible format and distinct bind flags");
            paintPacked(0xff000000,0xff000000);packed.Reset();
            check(cache.Finish(key),"finish after packed copies and native source release");
            borrowed=cache.AcquireCompleted(key,context.Get(),complete);
            check(borrowed,"HUD-complete packed pair available for submission");
            if (borrowed)
            {
                check(complete.tracking.serial==serial-1&&complete.tracking.spaceEpoch==7&&
                    complete.covers[0].halfX==1.0f&&complete.covers[1].halfY==.9f,
                    "packed capture retains world preparation tracking and eye covers");
                check(Pixels(device.Get(),context.Get(),complete.eyes[0],complete.descriptor,0xffa1b2c3)&&
                    Pixels(device.Get(),context.Get(),complete.eyes[1],complete.descriptor,0xffd4e5f6),
                    "packed top/bottom copies replace both world eyes and survive source recycling");
                check(cache.ReleaseCompleted(complete.borrowId),"release packed native HUD pair");
            }
        }
    }
    {
        // MONO (ClassicViewPair::mono): one capture, as eye 0, completes the frame, and its picture is handed out
        // for both eyes. The next stereo frame uses both banks again; a stereo frame never finishes on one eye.
        ClassicViewPair pair{};
        pair.source.player=0;
        Camera camera{};
        camera.position={1,2,3}; camera.forward={1,0,0}; camera.up={0,0,1}; camera.verticalFov=1.2f;
        camera.viewport=camera.window={0,0,static_cast<int16_t>(d.Height),static_cast<int16_t>(d.Width)};
        camera.nearPlane=0.01f; camera.farPlane=100;
        pair.source.render=pair.source.raster=camera;
        pair.eyes[0]=pair.eyes[1]=pair.source;
        pair.tracking.serial=300; pair.tracking.generation=3; pair.tracking.spaceEpoch=7;
        pair.rendererEpoch=1; pair.cover={1.8f,1.0f,0.9f}; pair.mono=true;
        check(cache.Begin(pair,key),"mono Classic frame begins");
        paint(0xff708090);
        check(cache.Capture(key,0,context.Get(),source.Get(),d)&&cache.Finish(key),
            "one capture completes a mono frame");
        borrowed=cache.AcquireCompleted(key,context.Get(),complete);
        check(borrowed&&complete.monoEye==0&&complete.eyes[0]&&complete.eyes[0]==complete.eyes[1]&&
            !complete.depthViews[0]&&!complete.depthViews[1],"the mono picture is handed out for both eyes");
        if (borrowed)
        {
            check(Pixels(device.Get(),context.Get(),complete.eyes[1],complete.descriptor,0xff708090),
                "both eyes show the one pass");
            check(cache.ReleaseCompleted(complete.borrowId),"release mono borrow");
        }
        pair.mono=false; pair.tracking.serial=301;
        check(cache.Begin(pair,key),"stereo Classic frame after mono");
        paint(0xff111111);
        check(cache.Capture(key,0,context.Get(),source.Get(),d),"stereo left after mono");
        paint(0xff222222);
        check(cache.Capture(key,1,context.Get(),source.Get(),d)&&cache.Finish(key),"stereo pair after mono");
        borrowed=cache.AcquireCompleted(key,context.Get(),complete);
        check(borrowed&&complete.monoEye==-1&&complete.eyes[0]!=complete.eyes[1],"a pair is two pictures again");
        if (borrowed)
        {
            check(Pixels(device.Get(),context.Get(),complete.eyes[0],complete.descriptor,0xff111111)&&
                Pixels(device.Get(),context.Get(),complete.eyes[1],complete.descriptor,0xff222222),
                "each eye of the pair after mono holds its own pass");
            check(cache.ReleaseCompleted(complete.borrowId),"release pair borrow");
        }
        pair.tracking.serial=302;
        check(cache.Begin(pair,key)&&cache.Capture(key,0,context.Get(),source.Get(),d)&&!cache.Finish(key),
            "a stereo frame never finishes on one eye");
    }
    changed=d; changed.SampleDesc.Count=2;
    check(!cache.Prepare(device.Get(),context.Get(),changed,3,3),"MSAA needs a separate verified resolve path");
    check(!cache.Begin(Receipt(107,d.Width,d.Height),key),"failed preparation retains no old admission");
    check(!cache.Prepare(device.Get(),deferred.Get(),d,3,3),"deferred context rejected cold");
    check(cache.Prepare(device.Get(),context.Get(),d,3,3),"cold failure recovers");
    check(cache.Reset()&&!cache.Prepare(device.Get(),context.Get(),d,3,3),
        "retirement does not permit reuse of an old resource epoch");
    return failures?1:0;
}
