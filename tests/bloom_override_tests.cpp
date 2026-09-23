#include "../src/dll/bloom_override.h"
#include "../src/common/bloom_override_logic.h"
#include "../src/common/config.h"
#include <wrl/client.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <thread>

using Microsoft::WRL::ComPtr;
using namespace bloom_override;
static GameTitle title=GameTitle::Halo3;
static ID3D11DeviceContext* eyeContext{};
static bool eyeActive=true;
static unsigned checks{},sets{};
GameTitle TitleAdapter_GetActiveTitle(){return title;}
bool VR_IsEyeRasterActive(ID3D11DeviceContext* context) noexcept {return eyeActive&&context==eyeContext;}
void Logf(const char*,...){}
static void Check(bool ok,const char* why){++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
static void STDMETHODCALLTYPE Set(ID3D11DeviceContext* context,UINT start,UINT count,ID3D11ShaderResourceView* const* views)
{++sets;context->PSSetShaderResources(start,count,views);}
static ComPtr<ID3D11ShaderResourceView> View2D(ID3D11Device* device,unsigned w,unsigned h,bool cache=true)
{
    std::vector<uint32_t> pixels(size_t(w)*h,0xFFFFFFFFu);
    D3D11_TEXTURE2D_DESC desc{};desc.Width=w;desc.Height=h;desc.MipLevels=1;desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;
    desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
    D3D11_SUBRESOURCE_DATA data{pixels.data(),w*4,0};ComPtr<ID3D11Texture2D> texture;
    Check(SUCCEEDED(device->CreateTexture2D(&desc,&data,&texture)),"synthetic color source creation");
    ComPtr<ID3D11ShaderResourceView> view;
    Check(SUCCEEDED(device->CreateShaderResourceView(texture.Get(),nullptr,&view)),"synthetic SRV creation");
    if(cache)Bloom_ObserveSrvCreated(device,view.Get());
    return view;
}
static void Bind(ID3D11DeviceContext* context,ID3D11ShaderResourceView* a,ID3D11ShaderResourceView* b,ID3D11PixelShader* shader)
{
    ID3D11ShaderResourceView* pair[]{a,b};context->PSSetShaderResources(0,2,pair);
    Bloom_ObserveResources(context,0,2,pair);Bloom_ObserveShader(context,shader,0);
}
static bool BoundIs(ID3D11DeviceContext* context,unsigned slot,ID3D11ShaderResourceView* expected)
{
    ComPtr<ID3D11ShaderResourceView> actual;context->PSGetShaderResources(slot,1,&actual);return actual.Get()==expected;
}
static void CheckZero(ID3D11Device* device,ID3D11DeviceContext* context,unsigned slot)
{
    ComPtr<ID3D11ShaderResourceView> view;context->PSGetShaderResources(slot,1,&view);
    ComPtr<ID3D11Resource> resource;view->GetResource(&resource);
    ComPtr<ID3D11Texture2D> texture;Check(SUCCEEDED(resource.As(&texture)),"substituted view has real texture backing");
    D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.BindFlags=0;
    ComPtr<ID3D11Texture2D> readback;Check(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&readback)),"zero readback creation");
    context->CopyResource(readback.Get(),texture.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
    Check(SUCCEEDED(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped)),"zero readback mapping");
    bool allZero=true;
    for(UINT y=0;y<desc.Height;++y)for(UINT x=0;x<desc.Width*4;++x)
        allZero=allZero&&static_cast<const uint8_t*>(mapped.pData)[size_t(y)*mapped.RowPitch+x]==0;
    context->Unmap(readback.Get(),0);Check(allZero,"every RGBA channel of precreated substitute is zero");
}
int main()
{
    View large{364,263,28,4},minor{91,66,28,4};
    for(const auto supported:{GameTitle::Halo3,GameTitle::Halo3ODST}) {
        Check(Slots(supported,kHalo3OdstConsumer,large,minor)==3,"donor H3/ODST rounded pyramid replaces both slots");
        Check(Slots(supported,kReachConsumer,large,minor)==0,"wrong-title consumer is not inferred from dimensions");
        auto wrong=minor;wrong.format=10;Check(!Slots(supported,kHalo3OdstConsumer,large,wrong),"mixed formats retain stock");
        wrong=minor;wrong.width+=2;Check(!Slots(supported,kHalo3OdstConsumer,large,wrong),"unproved pyramid topology retains stock");
    }
    Check(Slots(GameTitle::HaloReach,kReachConsumer,{},View{72,45,28,4})==2,"Reach exact 72x45 consumer only");
    Check(!Slots(GameTitle::HaloReach,kReachConsumer,{},View{73,45,28,4}),"Reach dimensions cannot be broadened");
    Check(!Slots(GameTitle::Halo4,kHalo3OdstConsumer,large,minor),"unsupported title remains native");
    Check(TextureBytes(16384,16384,15,16)>kCacheBudget&&TextureBytes(64,64,1,4)==16384,
        "full source footprint uses wide bounded arithmetic");
    Check(!TextureBytes(0xFFFFFFFFu,1,1,4)&&!TextureBytes(64,64,0,4),"invalid resource footprint rejected");
    ReaderGate gate;
    Check(gate.Enter()&&!gate.Write(),"cold drain cannot evict an active draw reader");gate.Leave();
    Check(gate.Write()&&!gate.Enter(),"draw must stay native while cold writer owns cache");gate.EndWrite();
    Check(gate.Enter(),"reader admission recovers after cold writer");gate.Leave();
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    Check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)),
        "synthetic WARP device creation (no MCC process)");
    eyeContext=context.Get();Bloom_SetAvailable(true,&Set);title=GameTitle::None;Bloom_ColdTick(title,0);
    // Startup resources can precede title discovery. A full-scene allocation
    // must not permanently occupy the budget ahead of smaller bloom views.
    auto scene=View2D(device.Get(),4096,2048);
    auto a=View2D(device.Get(),128,128),b=View2D(device.Get(),32,32);
    title=GameTitle::Halo3;Bloom_ColdTick(title,1);
    // Opaque synthetic shader identities are never dereferenced or passed to
    // D3D. These four-byte CRC fixtures test the real creation observer without
    // redistributing proprietary shader bytecode.
    auto shader=reinterpret_cast<ID3D11PixelShader*>(uintptr_t(0x12340));
    const uint8_t h3Bytes[]{0x2e,0xde,0x94,0xed},reachBytes[]{0x48,0x27,0xa9,0x7e};
    Check(Crc32(h3Bytes,sizeof(h3Bytes))==kHalo3OdstConsumer&&Crc32(reachBytes,sizeof(reachBytes))==kReachConsumer,
        "independent known CRC fixtures");
    Bloom_ObserveShaderCreated(shader,h3Bytes,sizeof(h3Bytes));Bind(context.Get(),a.Get(),b.Get(),shader);
    sets=0;{BloomDrawScope scope(context.Get());}Check(sets==0,"default bloom setting performs no substitution");
    g_config.bloom_enabled[0]=false;
    eyeActive=false;{BloomDrawScope scope(context.Get());}Check(sets==0,"non-eye drawing remains native");eyeActive=true;
    {
        BloomDrawScope scope(context.Get());
        Check(sets==1&&!BoundIs(context.Get(),0,a.Get())&&!BoundIs(context.Get(),1,b.Get()),"both bloom slots replaced together");
        CheckZero(device.Get(),context.Get(),0);CheckZero(device.Get(),context.Get(),1);
        // A resize drain attempts to run while restoration still owns a
        // reader. It must remain pending, leaving both originals alive.
        Bloom_BeforeResize();
    }
    Check(sets==2&&BoundIs(context.Get(),0,a.Get())&&BoundIs(context.Get(),1,b.Get()),"original SRVs restored after deferred resize drain");
    Bloom_ColdTick(GameTitle::None,0);sets=0;
    {BloomDrawScope scope(context.Get());}Check(sets==0,"drained cache declines drawing rather than restoring stale pointers");
    Bloom_ColdTick(title,2);Bloom_ObserveSrvCreated(device.Get(),a.Get());Bloom_ObserveSrvCreated(device.Get(),b.Get());
    Bind(context.Get(),a.Get(),b.Get(),shader);Bloom_Invalidate(context.Get(),false);
    {BloomDrawScope scope(context.Get());}Check(sets==0,"OM hazard invalidation requires fresh SRV bindings");
    Bind(context.Get(),a.Get(),b.Get(),shader);Bloom_Invalidate(context.Get());
    {BloomDrawScope scope(context.Get());}Check(sets==0,"command-list/context-state invalidation requires fresh shader state");
    const uint8_t unrelated[]{0,0,0,0};Bloom_ObserveShaderCreated(shader,unrelated,sizeof(unrelated));
    Bind(context.Get(),a.Get(),b.Get(),shader);{BloomDrawScope scope(context.Get());}
    Check(sets==0,"unrelated shader reusing pointer cannot inherit bloom identity");
    Bloom_ObserveShaderCreated(shader,h3Bytes,sizeof(h3Bytes));
    auto oversized=View2D(device.Get(),4096,4096),quarter=View2D(device.Get(),1024,1024);
    Bind(context.Get(),oversized.Get(),quarter.Get(),shader);
    {BloomDrawScope scope(context.Get());}
    Check(sets==0&&BoundIs(context.Get(),0,oversized.Get())&&BoundIs(context.Get(),1,quarter.Get()),
        "valid pyramid exceeding combined source/zero 64MiB cap remains wholly native");
    // Incoming resources are acquired before the worker sees the new title.
    // Lifecycle observation must preserve them until ordinary device teardown.
    auto r=View2D(device.Get(),72,45);title=GameTitle::HaloReach;Bloom_ColdTick(title,1);
    Bloom_ObserveSrvCreated(device.Get(),a.Get());
    Bloom_ObserveShaderCreated(shader,reachBytes,sizeof(reachBytes));Bind(context.Get(),a.Get(),r.Get(),shader);
    g_config.bloom_enabled[2]=false;
    {BloomDrawScope scope(context.Get());Check(BoundIs(context.Get(),0,a.Get())&&!BoundIs(context.Get(),1,r.Get()),"Reach changes slot1 only");}
    Check(BoundIs(context.Get(),0,a.Get())&&BoundIs(context.Get(),1,r.Get()),"Reach restores exact original pair");
    title=GameTitle::Halo3;Bloom_ColdTick(title,3);
    Bloom_ObserveShaderCreated(shader,h3Bytes,sizeof(h3Bytes));Bloom_ObserveSrvCreated(device.Get(),b.Get());
    ComPtr<IDXGIDevice> dxgiDevice;ComPtr<IDXGIAdapter> adapter;ComPtr<IDXGIFactory> factory;
    Check(SUCCEEDED(device.As(&dxgiDevice))&&SUCCEEDED(dxgiDevice->GetAdapter(&adapter))&&
        SUCCEEDED(adapter->GetParent(__uuidof(IDXGIFactory),reinterpret_cast<void**>(factory.GetAddressOf()))),"synthetic swapchain factory");
    HWND window=CreateWindowExW(0,L"STATIC",L"MCC VR bloom test (hidden)",WS_POPUP,0,0,128,128,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    Check(window!=nullptr,"hidden synthetic window creation");
    DXGI_SWAP_CHAIN_DESC swapDesc{};swapDesc.BufferDesc.Width=128;swapDesc.BufferDesc.Height=128;
    swapDesc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;swapDesc.SampleDesc.Count=1;
    swapDesc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT|DXGI_USAGE_SHADER_INPUT;swapDesc.BufferCount=1;
    swapDesc.OutputWindow=window;swapDesc.Windowed=TRUE;swapDesc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
    ComPtr<IDXGISwapChain> swapchain;
    Check(SUCCEEDED(factory->CreateSwapChain(device.Get(),&swapDesc,&swapchain)),"hidden WARP swapchain creation");
    ComPtr<ID3D11Texture2D> buffer;ComPtr<ID3D11ShaderResourceView> bufferView;
    Check(SUCCEEDED(swapchain->GetBuffer(0,__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(buffer.GetAddressOf())))&&
        SUCCEEDED(device->CreateShaderResourceView(buffer.Get(),nullptr,&bufferView)),"shader-readable swapchain buffer acquisition");
    Bloom_ObserveSrvCreated(device.Get(),bufferView.Get());Bind(context.Get(),bufferView.Get(),b.Get(),shader);
    sets=0;{BloomDrawScope scope(context.Get());}
    Check(sets==0&&BoundIs(context.Get(),0,bufferView.Get()),"DXGI backbuffer is refused despite matching shader/pyramid dimensions");
    context->ClearState();bufferView.Reset();buffer.Reset();
    Check(SUCCEEDED(swapchain->ResizeBuffers(1,256,256,DXGI_FORMAT_UNKNOWN,0)),
        "cache never pins swapchain buffer or blocks native resize");
    swapchain.Reset();DestroyWindow(window);
    Bloom_BeforeResize();context->ClearState();
    std::printf("Bloom override: %u checks passed (real WARP resources, no game install/launch).\n",checks);
}
