// D3D11 context draw variants not covered by the existing Draw/DrawIndexed
// reticle hooks. Off-scope calls forward their complete original ABI.
using HudDrawIndexedInstancedFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,UINT,INT,UINT);
using HudDrawInstancedFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,UINT,UINT);
using HudDrawAutoFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*);
using HudDrawIndirectFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Buffer*,UINT);
static HudDrawIndexedInstancedFn g_origHudDrawIndexedInstanced{};
static HudDrawInstancedFn g_origHudDrawInstanced{};
static HudDrawAutoFn g_origHudDrawAuto{};
static HudDrawIndirectFn g_origHudDrawIndexedIndirect{},g_origHudDrawIndirect{};

static void STDMETHODCALLTYPE HudDrawIndexedInstancedHook(ID3D11DeviceContext* context,
    UINT indices,UINT instances,UINT startIndex,INT baseVertex,UINT startInstance)
{
    if(ShouldHideExtraHudDraw(context))return;
    BloomDrawScope bloom(context);
    g_origHudDrawIndexedInstanced(context,indices,instances,startIndex,baseVertex,startInstance);
}
static void STDMETHODCALLTYPE HudDrawInstancedHook(ID3D11DeviceContext* context,
    UINT vertices,UINT instances,UINT startVertex,UINT startInstance)
{
    if(ShouldHideExtraHudDraw(context))return;
    BloomDrawScope bloom(context);
    g_origHudDrawInstanced(context,vertices,instances,startVertex,startInstance);
}
static void STDMETHODCALLTYPE HudDrawAutoHook(ID3D11DeviceContext* context)
{
    if(ShouldHideExtraHudDraw(context))return;
    BloomDrawScope bloom(context);
    g_origHudDrawAuto(context);
}
static void STDMETHODCALLTYPE HudDrawIndexedIndirectHook(ID3D11DeviceContext* context,ID3D11Buffer* arguments,UINT offset)
{
    if(ShouldHideExtraHudDraw(context))return;
    BloomDrawScope bloom(context);
    g_origHudDrawIndexedIndirect(context,arguments,offset);
}
static void STDMETHODCALLTYPE HudDrawIndirectHook(ID3D11DeviceContext* context,ID3D11Buffer* arguments,UINT offset)
{
    if(ShouldHideExtraHudDraw(context))return;
    BloomDrawScope bloom(context);
    g_origHudDrawIndirect(context,arguments,offset);
}

static bool InstallExtraHudDrawHooks(void** contextVtbl)
{
    bool complete=true;
    struct Binding {size_t slot;void* detour;void** original;};
    const Binding bindings[]{
        {20,reinterpret_cast<void*>(&HudDrawIndexedInstancedHook),reinterpret_cast<void**>(&g_origHudDrawIndexedInstanced)},
        {21,reinterpret_cast<void*>(&HudDrawInstancedHook),reinterpret_cast<void**>(&g_origHudDrawInstanced)},
        {38,reinterpret_cast<void*>(&HudDrawAutoHook),reinterpret_cast<void**>(&g_origHudDrawAuto)},
        {39,reinterpret_cast<void*>(&HudDrawIndexedIndirectHook),reinterpret_cast<void**>(&g_origHudDrawIndexedIndirect)},
        {40,reinterpret_cast<void*>(&HudDrawIndirectHook),reinterpret_cast<void**>(&g_origHudDrawIndirect)}
    };
    for(const auto& binding:bindings)
    {
        const auto status=MH_CreateHook(contextVtbl[binding.slot],binding.detour,binding.original);
        if(status!=MH_OK) {
            complete=false;
            LOG("Gameplay HUD hiding: optional D3D11 draw slot %zu unavailable (%d); only that draw variant stays stock",
                binding.slot,int(status));
        }
    }
    return complete;
}
