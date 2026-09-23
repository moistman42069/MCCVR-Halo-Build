#include <Windows.h>
#include <d3d11.h>
#include <atomic>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <climits>
#include "../src/common/dlss_sampler_logic.h"
static unsigned factoryCalls{},bindCalls{},checks{};
static ID3D11SamplerState* bound{};
static bool failVariants{};
static void Check(bool ok,const char* label)
{++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",label);std::exit(1);}}
static HRESULT STDMETHODCALLTYPE OriginalCreate(ID3D11Device* device,const D3D11_SAMPLER_DESC* desc,ID3D11SamplerState** output)
{++factoryCalls;if(failVariants&&factoryCalls%6!=1){*output=nullptr;return E_OUTOFMEMORY;}return device->CreateSamplerState(desc,output);}
static void STDMETHODCALLTYPE OriginalBind(ID3D11DeviceContext*,UINT,UINT count,ID3D11SamplerState* const* samplers)
{++bindCalls;bound=count&&samplers?samplers[0]:nullptr;}
static auto g_origCreateSamplerState=&OriginalCreate;
static auto g_origPSSetSamplers=&OriginalBind;
#include "../src/dll/dlss_sampler_cache.inl"
int main()
{
    ID3D11Device* device=nullptr;ID3D11DeviceContext* context=nullptr;
    Check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
        D3D11_SDK_VERSION,&device,nullptr,&context)),"offline WARP device creation");
    D3D11_SAMPLER_DESC desc{};desc.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    desc.AddressU=desc.AddressV=desc.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
    desc.ComparisonFunc=D3D11_COMPARISON_ALWAYS;desc.MaxLOD=D3D11_FLOAT32_MAX;desc.MaxAnisotropy=1;
    ID3D11SamplerState* sampler=nullptr;
    Check(SUCCEEDED(CreateSamplerStateHook(device,&desc,&sampler))&&factoryCalls==6,
        "factory creates original plus five finite mode variants once");
    const unsigned coldCalls=factoryCalls;
    for(unsigned mode=0;mode<dlss_sampler::kModes;++mode)
    {
        D3D_SetEyeSamplerBias(dlss_sampler::kBias[mode]);
        for(int frame=0;frame<200;++frame)
        {
            PSSetSamplersHook(context,0,1,&sampler);
            Check(bound&&bound!=sampler,"owned eye bind selects a prebuilt sampler");
        }
        D3D11_SAMPLER_DESC actual{};bound->GetDesc(&actual);
        Check(std::fabs(actual.MipLODBias-dlss_sampler::kBias[mode])<.00001f,
            "all quality modes preserve the required bias");
        Check(actual.Filter==desc.Filter&&actual.AddressU==desc.AddressU&&actual.MaxLOD==desc.MaxLOD,
            "bias variant preserves unrelated native sampler fields");
        Check(factoryCalls==coldCalls,"bind and mode transitions perform no sampler creation");
        std::thread other([&]{PSSetSamplersHook(context,0,1,&sampler);});other.join();
        Check(bound==sampler,"another thread retains original sampler");
        D3D_SetEyeSamplerBias(0);PSSetSamplersHook(context,0,1,&sampler);
        Check(bound==sampler,"eye end returns exact original sampler");
    }
    D3D_SetEyeSamplerBias(-5);PSSetSamplersHook(context,0,1,&sampler);
    Check(bound==sampler,"unproven ratio leaves native mips rather than allocating in a bind");
    D3D_SetEyeSamplerBias(-1);ID3D11SamplerState* missing=nullptr;
    desc.MipLODBias=.314f;Check(SUCCEEDED(device->CreateSamplerState(&desc,&missing)),"uncached sampler fixture");
    PSSetSamplersHook(context,0,1,&missing);Check(bound==missing,"uncached creation remains stock");
    ID3D11SamplerState* duplicate=nullptr;
    desc.MipLODBias=0;Check(SUCCEEDED(CreateSamplerStateHook(device,&desc,&duplicate)),"deduplicated native source");
    Check(duplicate==sampler&&factoryCalls==coldCalls+1,"repeated native creation reuses immutable variants");
    PSSetSamplersHook(context,15,2,&sampler);Check(bound==sampler,"invalid slot range is forwarded without inspecting extra pointers");
    factoryCalls=0;failVariants=true;desc.MipLODBias=.25f;
    ID3D11SamplerState* failed=nullptr;
    Check(SUCCEEDED(CreateSamplerStateHook(device,&desc,&failed))&&failed,
        "optional variant allocation failures preserve the game's sampler creation");
    D3D_SetEyeSamplerBias(-2);PSSetSamplersHook(context,0,1,&failed);
    Check(bound==failed&&g_samplerFailures.load()>=5,"failed variants retain native rendering and record worker diagnostics");
    failed->Release();
    duplicate->Release();missing->Release();sampler->Release();context->Release();device->Release();
    std::printf("DLSS sampler cache: %u checks passed, %u native bindings, %u cold factory calls\n",checks,bindCalls,factoryCalls);
}
