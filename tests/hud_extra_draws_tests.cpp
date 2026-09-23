#define CINTERFACE
#define D3D11_NO_HELPERS
#include <Windows.h>
#include <d3d11.h>
#include <MinHook.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include "../src/common/hud_visibility.h"

// The actual SDK COM layout is the authority for these five slots.
static_assert(offsetof(ID3D11DeviceContextVtbl,DrawIndexedInstanced)/sizeof(void*)==20);
static_assert(offsetof(ID3D11DeviceContextVtbl,DrawInstanced)/sizeof(void*)==21);
static_assert(offsetof(ID3D11DeviceContextVtbl,DrawAuto)/sizeof(void*)==38);
static_assert(offsetof(ID3D11DeviceContextVtbl,DrawIndexedInstancedIndirect)/sizeof(void*)==39);
static_assert(offsetof(ID3D11DeviceContextVtbl,DrawInstancedIndirect)/sizeof(void*)==40);

static unsigned checks{},calls{},installs{},failSlot=UINT_MAX,logged{};
static std::array<uintptr_t,7> received{};
static void Check(bool value,const char* message)
{++checks;if(!value){std::fprintf(stderr,"HUD draw coverage: %s\n",message);std::exit(1);}}
static bool ShouldHideExtraHudDraw(ID3D11DeviceContext*){return hud_visibility::Hidden();}
static unsigned bloomScopes{},bloomActive{};
// GPU replacement/restoration is covered by bloom_override_tests. This fixture
// verifies every forwarded draw participates in the surrounding scope.
struct BloomDrawScope {
    explicit BloomDrawScope(ID3D11DeviceContext*) {++bloomScopes;++bloomActive;}
    ~BloomDrawScope(){--bloomActive;}
};
static void STDMETHODCALLTYPE Indexed(ID3D11DeviceContext* c,UINT a,UINT b,UINT d,INT e,UINT f)
{++calls;received={0,reinterpret_cast<uintptr_t>(c),a,b,d,uintptr_t(intptr_t(e)),f};}
static void STDMETHODCALLTYPE Instanced(ID3D11DeviceContext* c,UINT a,UINT b,UINT d,UINT e)
{++calls;received={1,reinterpret_cast<uintptr_t>(c),a,b,d,e,0};}
static void STDMETHODCALLTYPE Auto(ID3D11DeviceContext* c)
{++calls;received={2,reinterpret_cast<uintptr_t>(c),0,0,0,0,0};}
static void STDMETHODCALLTYPE IndexedIndirect(ID3D11DeviceContext* c,ID3D11Buffer* b,UINT offset)
{++calls;received={3,reinterpret_cast<uintptr_t>(c),reinterpret_cast<uintptr_t>(b),offset,0,0,0};}
static void STDMETHODCALLTYPE Indirect(ID3D11DeviceContext* c,ID3D11Buffer* b,UINT offset)
{++calls;received={4,reinterpret_cast<uintptr_t>(c),reinterpret_cast<uintptr_t>(b),offset,0,0,0};}
static void* expectedOriginals[]{reinterpret_cast<void*>(&Indexed),reinterpret_cast<void*>(&Instanced),
    reinterpret_cast<void*>(&Auto),reinterpret_cast<void*>(&IndexedIndirect),reinterpret_cast<void*>(&Indirect)};
static constexpr unsigned slots[]{20,21,38,39,40};
static MH_STATUS Create(void* target,void* detour,void** original)
{
    const unsigned i=installs++;Check(i<5,"bounded hook inventory");
    Check(target==expectedOriginals[i]&&detour&&original,"matching method binding");
    if(slots[i]==failSlot)return MH_ERROR_UNSUPPORTED_FUNCTION;
    *original=target;return MH_OK;
}
#define MH_CreateHook Create
#define LOG(...) (++logged)
#include "../src/dll/hud_extra_draws.inl"
#undef LOG
#undef MH_CreateHook

static void Invoke(unsigned i,ID3D11DeviceContext* c,ID3D11Buffer* b)
{
    switch(i){
    case 0:HudDrawIndexedInstancedHook(c,UINT_MAX,7,9,-29,31);break;
    case 1:HudDrawInstancedHook(c,UINT_MAX,7,9,31);break;
    case 2:HudDrawAutoHook(c);break;
    case 3:HudDrawIndexedIndirectHook(c,b,0xfffffffcu);break;
    case 4:HudDrawIndirectHook(c,b,0xfffffffcu);break;
    }
}
int main()
{
    std::array<void*,115> vtable{};
    for(unsigned i=0;i<5;++i)vtable[slots[i]]=expectedOriginals[i];
    for(unsigned rejected=0;rejected<6;++rejected){
        installs=logged=0;failSlot=rejected<5?slots[rejected]:UINT_MAX;
        InstallExtraHudDrawHooks(vtable.data());
        Check(installs==5&&logged==(rejected<5?1u:0u),"optional failure leaves other draw bindings attempted and is reported");
    }
    auto* context=reinterpret_cast<ID3D11DeviceContext*>(uintptr_t(0x12345678));
    auto* buffer=reinterpret_cast<ID3D11Buffer*>(uintptr_t(0x76543210));
    for(unsigned i=0;i<5;++i){
        const unsigned before=calls,scopesBefore=bloomScopes;Invoke(i,context,buffer);
        Check(bloomScopes==scopesBefore+1&&!bloomActive,"forwarded draw restores bloom scope");
        Check(calls==before+1&&received[0]==i&&received[1]==reinterpret_cast<uintptr_t>(context),"ordinary draw forwards exactly once");
        if(i==0)Check(received[2]==UINT_MAX&&received[3]==7&&received[4]==9&&received[5]==uintptr_t(intptr_t(-29))&&received[6]==31,"indexed instancing preserves all arguments including signed base vertex");
        if(i==1)Check(received[2]==UINT_MAX&&received[3]==7&&received[4]==9&&received[5]==31,"instancing preserves start vertex and instance");
        if(i>=3)Check(received[2]==reinterpret_cast<uintptr_t>(buffer)&&received[3]==0xfffffffcu,"indirect draws preserve buffer identity and byte offset");
        hud_visibility::depth=1;Invoke(i,context,buffer);
        ++hud_visibility::depth;Invoke(i,context,buffer);--hud_visibility::depth;Invoke(i,context,buffer);
        Check(calls==before+1,"nested gameplay HUD scopes omit GPU submission");
        Check(bloomScopes==scopesBefore+1&&!bloomActive,"hidden draws do not enter bloom scope");
        hud_visibility::depth=0;Invoke(i,context,buffer);
        Check(calls==before+2,"later world/menu draw resumes forwarding");
    }
    hud_visibility::depth=1;bool otherHidden=true;
    std::thread other([&]{otherHidden=hud_visibility::Hidden();});other.join();
    Check(!otherHidden&&hud_visibility::Hidden(),"HUD suppression cannot leak to another rendering thread");
    hud_visibility::depth=0;
    std::printf("PASS: %u production HUD draw forwarding checks\n",checks);
}
