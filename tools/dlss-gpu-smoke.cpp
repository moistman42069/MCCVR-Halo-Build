// Optional hardware integration check. No MCC process, injection or headset.
// Uses the same production NGX wrapper, both eye slots and live size changes.
#include "../src/dll/dlss.h"
#include "../src/common/log.h"
#include "../src/common/dlss_logic.h"
#include <windows.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <cstdio>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <cwchar>
using Microsoft::WRL::ComPtr;
static void Require(bool ok,const char* message) {
    if(!ok) {std::fprintf(stderr,"FAIL: %s (%s)\n",message,Dlss_StatusText());Dlss_Shutdown();std::exit(1);}
}
int wmain(int argc,wchar_t** argv) {
    if(argc<2||argc>3||(argc==3&&std::wcscmp(argv[2],L"--benchmark")&&std::wcscmp(argv[2],L"--all-models"))) {
        std::fprintf(stderr,"Usage: dlss-gpu-smoke <isolated runtime and log directory> [--benchmark|--all-models]\n");return 2;
    }
    const bool benchmark=argc==3&&!std::wcscmp(argv[2],L"--benchmark");
    const bool allModels=argc==3&&!std::wcscmp(argv[2],L"--all-models");
    const auto folder=std::filesystem::absolute(argv[1]);
    Require(std::filesystem::is_regular_file(folder/L"nvngx_dlss.dll"),"pinned runtime must be staged explicitly");
    LogInit((folder/L"dlss-gpu-smoke.log").c_str());
    ComPtr<IDXGIFactory1> factory;ComPtr<IDXGIAdapter1> adapter;
    Require(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))),"DXGI factory");
    DXGI_ADAPTER_DESC1 adapterInfo{};bool found=false;
    for(UINT index=0;factory->EnumAdapters1(index,&adapter)==S_OK;++index) {
        adapter->GetDesc1(&adapterInfo);
        if(adapterInfo.VendorId==0x10de && !(adapterInfo.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)) {found=true;break;}
        adapter.Reset();
    }
    if(!found) {std::puts("SKIP: NVIDIA hardware unavailable");return 77;}
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};
    Require(SUCCEEDED(D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,0,nullptr,0,
        D3D11_SDK_VERSION,&device,&level,&context)),"NVIDIA D3D11 device");
    if(!Dlss_EnsureInitialized(device.Get())) {std::printf("UNAVAILABLE: %s\n",Dlss_StatusText());Dlss_Shutdown();return 77;}
    std::wprintf(L"GPU: %ls\n",adapterInfo.Description);
    unsigned evaluations=0;
    const std::vector<unsigned> widths=benchmark?std::vector<unsigned>{3292u}:allModels?std::vector<unsigned>{1024u}:
        std::vector<unsigned>{768u,1024u,768u};
    for(int preset=allModels?0:dlss::kPresetCnn;preset<=dlss::kPresetMax;++preset)
    for(unsigned outputW:widths) for(int quality=allModels?1:0;quality<(allModels?2:5);++quality) {
        const unsigned outputH=benchmark?2374u:outputW*2/3;
        uint32_t minW{},minH{},maxW{},maxH{},renderW{},renderH{};
        Require(Dlss_QueryRenderRange(outputW,outputH,quality,minW,minH,maxW,maxH,renderW,renderH),"NGX render-range query");
        Require(renderW&&renderH&&renderW>=minW&&renderW<=maxW&&renderH>=minH&&renderH<=maxH,"valid optimal dimensions");
        for(int eye=0;eye<2;++eye) {
            DlssFeatureDesc feature{renderW,renderH,outputW,outputH,quality,preset,false};
            bool recreated=false;char reason[256]{};
            Require(Dlss_EnsureFeature(eye,context.Get(),feature,recreated,reason,sizeof(reason)),reason);
            Require(recreated,"changing size or mode rebuilds that eye's feature");
            auto texture=[&](UINT w,UINT h,DXGI_FORMAT format,UINT bind,const void* data,UINT pitch) {
                D3D11_TEXTURE2D_DESC desc{};desc.Width=w;desc.Height=h;desc.MipLevels=desc.ArraySize=1;
                desc.Format=format;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=bind;
                D3D11_SUBRESOURCE_DATA initial{data,pitch,0};ComPtr<ID3D11Texture2D> result;
                Require(SUCCEEDED(device->CreateTexture2D(&desc,data?&initial:nullptr,&result)),"DLSS texture allocation");
                return result;
            };
            std::vector<uint32_t> pixels(size_t(renderW)*renderH,eye?0xff00ff00u:0xff0000ffu);
            std::vector<float> depths(pixels.size(),.5f);std::vector<uint32_t> motion(pixels.size(),0);
            auto color=texture(renderW,renderH,DXGI_FORMAT_R8G8B8A8_UNORM,D3D11_BIND_SHADER_RESOURCE,pixels.data(),renderW*4);
            auto depth=texture(renderW,renderH,DXGI_FORMAT_R32_FLOAT,D3D11_BIND_SHADER_RESOURCE,depths.data(),renderW*4);
            auto vectors=texture(renderW,renderH,DXGI_FORMAT_R16G16_FLOAT,D3D11_BIND_SHADER_RESOURCE,motion.data(),renderW*4);
            auto output=texture(outputW,outputH,DXGI_FORMAT_R8G8B8A8_UNORM,D3D11_BIND_UNORDERED_ACCESS|D3D11_BIND_SHADER_RESOURCE,nullptr,0);
            D3D11_VIEWPORT viewport{3,7,117,93,0,1};context->RSSetViewports(1,&viewport);
            ComPtr<ID3D11Query> disjoint,start,end;
            if(benchmark) {
                D3D11_QUERY_DESC desc{D3D11_QUERY_TIMESTAMP_DISJOINT,0};
                Require(SUCCEEDED(device->CreateQuery(&desc,&disjoint)),"GPU clock query");
                desc.Query=D3D11_QUERY_TIMESTAMP;
                Require(SUCCEEDED(device->CreateQuery(&desc,&start))&&
                        SUCCEEDED(device->CreateQuery(&desc,&end)),"GPU timestamp queries");
            }
            std::vector<double> samples;
            for(unsigned frame=0;frame<(benchmark?16u:3u);++frame) {
                DlssEvalInputs input{};input.color=color.Get();input.depth=depth.Get();input.motion=vectors.Get();input.output=output.Get();
                input.reset=frame==0;input.frameTimeMs=1000.f/90;
                if(benchmark) {context->Begin(disjoint.Get());context->End(start.Get());}
                uint32_t failure{};Require(Dlss_Evaluate(eye,context.Get(),input,failure),"DLSS evaluation");++evaluations;
                if(benchmark) {
                    context->End(end.Get());context->End(disjoint.Get());context->Flush();
                    auto wait=[&](ID3D11Query* query,void* value,UINT bytes) {
                        const auto deadline=GetTickCount64()+5000;HRESULT result=S_FALSE;
                        do {result=context->GetData(query,value,bytes,0);if(result==S_FALSE)Sleep(1);}
                        while(result==S_FALSE&&GetTickCount64()<deadline);
                        Require(result==S_OK,"bounded GPU timing readback");
                    };
                    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT clock{};UINT64 beginTick{},endTick{};
                    wait(disjoint.Get(),&clock,sizeof(clock));wait(start.Get(),&beginTick,sizeof(beginTick));wait(end.Get(),&endTick,sizeof(endTick));
                    Require(!clock.Disjoint&&clock.Frequency&&endTick>=beginTick,"coherent GPU timestamps");
                    if(frame>=4) samples.push_back(double(endTick-beginTick)*1000.0/double(clock.Frequency));
                }
                D3D11_VIEWPORT restored{};UINT count=1;context->RSGetViewports(&count,&restored);
                Require(count==1&&restored.TopLeftX==viewport.TopLeftX&&restored.TopLeftY==viewport.TopLeftY&&
                    restored.Width==viewport.Width&&restored.Height==viewport.Height,"production wrapper restores native viewport");
            }
            if(benchmark) {
                std::sort(samples.begin(),samples.end());double total=0;for(double ms:samples)total+=ms;
                std::printf("GPU_ONLY mode=%d eye=%d output=%ux%u input=%ux%u mean_ms=%.3f p95_ms=%.3f samples=%zu\n",
                    quality,eye,outputW,outputH,renderW,renderH,total/samples.size(),
                    samples[static_cast<size_t>(std::ceil(samples.size()*.95))-1],samples.size());
            }
            D3D11_TEXTURE2D_DESC readDesc{};output->GetDesc(&readDesc);readDesc.Usage=D3D11_USAGE_STAGING;
            readDesc.BindFlags=0;readDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> readback;
            Require(SUCCEEDED(device->CreateTexture2D(&readDesc,nullptr,&readback)),"readback allocation");
            context->CopyResource(readback.Get(),output.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
            Require(SUCCEEDED(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped)),"GPU completion/readback");
            const auto* p=static_cast<const unsigned char*>(mapped.pData)+(outputH/2)*mapped.RowPitch+(outputW/2)*4;
            const bool correct=eye?p[1]>128&&p[0]<64:p[0]>128&&p[1]<64;
            context->Unmap(readback.Get(),0);Require(correct,"fresh output belongs to the correct eye");
        }
        std::printf("PASS: preset=%s mode=%d render=%ux%u output=%ux%u both eyes\n",dlss::PresetName(preset),quality,renderW,renderH,outputW,outputH);
    }
    Dlss_Shutdown();
    Require(Dlss_EnsureInitialized(device.Get()),"wrapper reinitializes after teardown");Dlss_Shutdown();
    std::printf("PASS: %u hardware evaluations, feature changes, eye ownership and teardown\n",evaluations);
}
