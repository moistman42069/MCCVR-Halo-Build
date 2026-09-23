#include "../src/dll/haloce_eye_cache.h"
#include <wrl/client.h>
#include <cstdio>
#include <cstdlib>
#include <cmath>
using Microsoft::WRL::ComPtr;
static unsigned checks;
static void Check(bool ok,const char* reason){++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",reason);std::exit(1);}}
static halo_ce::PreparedReceipt Receipt(uint64_t serial) {
    halo_ce::PreparedReceipt r{};r.ticket={1,0x12340,3,halo_ce::PreparationOrigin::CopiedList};
    r.tracking.serial=r.pair.serial=serial;r.tracking.generation=r.pair.generation=3;
    r.tracking.spaceEpoch=r.pair.spaceEpoch=7;
    for(int eye=0;eye<2;++eye) {
        r.pair.cameras[eye].viewportWidth=32;r.pair.cameras[eye].viewportHeight=16;
        r.pair.covers[eye]={1.8f,1.f,.9f};
    }
    return r;
}
static bool DepthPixels(ID3D11Device* device,ID3D11DeviceContext* context,
    ID3D11ShaderResourceView* view,const D3D11_TEXTURE2D_DESC& descriptor,float expected) {
    ComPtr<ID3D11Resource> source;view->GetResource(&source);
    auto d=descriptor;d.BindFlags=0;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    if(FAILED(device->CreateTexture2D(&d,nullptr,&staging)))return false;
    context->CopyResource(staging.Get(),source.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
    if(FAILED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)))return false;
    bool ok=true;
    for(UINT y=0;y<d.Height;++y) for(UINT x=0;x<d.Width;++x)
        ok&=std::abs(reinterpret_cast<const float*>(static_cast<const uint8_t*>(mapped.pData)+y*mapped.RowPitch)[x]-expected)<.0001f;
    context->Unmap(staging.Get(),0);return ok;
}
int main() {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    Check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,
        &device,nullptr,&context)),"WARP device");
    D3D11_TEXTURE2D_DESC color{};color.Width=32;color.Height=16;color.MipLevels=color.ArraySize=1;
    color.SampleDesc.Count=1;color.Format=DXGI_FORMAT_R8G8B8A8_UNORM;color.BindFlags=D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> source;Check(SUCCEEDED(device->CreateTexture2D(&color,nullptr,&source)),"native color");
    auto depth=color;depth.Width=16;depth.Height=8;depth.Format=DXGI_FORMAT_R32_TYPELESS;
    depth.BindFlags=D3D11_BIND_DEPTH_STENCIL;
    ComPtr<ID3D11Texture2D> nativeDepth;ComPtr<ID3D11DepthStencilView> dsv;
    D3D11_DEPTH_STENCIL_VIEW_DESC ds{};ds.Format=DXGI_FORMAT_D32_FLOAT;ds.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
    Check(SUCCEEDED(device->CreateTexture2D(&depth,nullptr,&nativeDepth))&&
        SUCCEEDED(device->CreateDepthStencilView(nativeDepth.Get(),&ds,&dsv)),"native depth and DSV");
    halo_ce::EyeCache cache;halo_ce::EyeCache::Key key{};halo_ce::EyeCache::Completed pair{};
    Check(cache.Prepare(device.Get(),context.Get(),color,3,1),"color caches");
    Check(cache.PrepareDepth(device.Get(),context.Get(),depth,3),"separate native-raster depth banks");
    float p[16]{1,0,0,0,0,1,0,0,0,0,0,-1,0,0,.03f,0};
    const float pos[]{0,0,0},fwd[]{0,0,-1},up[]{0,1,0};
    const auto camera=dlss::MakeCameraSample(pos,fwd,up,p,0,0);Check(camera.valid,"depth camera");
    Check(cache.Begin(Receipt(100),key),"begin own pair");
    for(int eye=0;eye<2;++eye) {
        context->ClearDepthStencilView(dsv.Get(),D3D11_CLEAR_DEPTH,eye?.8f:.2f,0);
        Check(cache.CaptureDepth(key,eye,context.Get(),nativeDepth.Get(),depth,camera,41),"copy exact native eye depth");
        Check(cache.Capture(key,eye,context.Get(),source.Get(),color),"copy own eye color");
    }
    context->ClearDepthStencilView(dsv.Get(),D3D11_CLEAR_DEPTH,.5f,0);
    Check(cache.Finish(key)&&cache.AcquireCompleted(key,context.Get(),pair),"borrow matching color/depth/cameras");
    Check(pair.depthViews[0]&&pair.depthViews[1]&&pair.depthViews[0]!=pair.depthViews[1]&&
        pair.depthHistoryEpoch==41&&pair.depthCameras[0].valid&&pair.depthCameras[1].valid,"complete pair receipts");
    Check(DepthPixels(device.Get(),context.Get(),pair.depthViews[0],pair.depthDescriptor,.2f)&&
        DepthPixels(device.Get(),context.Get(),pair.depthViews[1],pair.depthDescriptor,.8f),"independent depth survives source overwrite");
    Check(!cache.PrepareDepth(device.Get(),context.Get(),depth,3)&&!cache.Reset(),"borrow excludes depth reallocation/retirement");
    Check(cache.ReleaseCompleted(pair.borrowId),"release complete pair");
    Check(cache.Begin(Receipt(101),key),"next color frame");
    auto stale=key;++stale.serial;
    Check(!cache.CaptureDepth(stale,0,context.Get(),nativeDepth.Get(),depth,camera,41),"stale depth key rejected");
    Check(cache.Capture(key,0,context.Get(),source.Get(),color)&&cache.Capture(key,1,context.Get(),source.Get(),color)&&
        cache.Finish(key)&&cache.AcquireCompleted(key,context.Get(),pair),"depth failure does not reject ordinary stereo");
    Check(!pair.depthViews[0]&&!pair.depthViews[1]&&!pair.depthHistoryEpoch,"no partial or old depth leaks into new colors");
    Check(cache.ReleaseCompleted(pair.borrowId),"release ordinary pair");
    auto bad=depth;bad.SampleDesc.Count=2;
    Check(!cache.PrepareDepth(device.Get(),context.Get(),bad,3),"unproved MSAA depth refused");
    Check(cache.Begin(Receipt(102),key)&&cache.Capture(key,0,context.Get(),source.Get(),color)&&
        cache.Capture(key,1,context.Get(),source.Get(),color)&&cache.Finish(key),"depth allocation refusal preserves color bank");
    Check(cache.Reset(),"cold retirement releases both bank types");
    std::printf("PASS: %u CE DLSS depth lease checks\n",checks);
}
