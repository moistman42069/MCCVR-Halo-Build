// Independent optional Anniversary flare transaction (E-CE-FLARE-1).
namespace ce_flare
{
using ProjectionFn=void(__fastcall*)(uintptr_t,uint64_t,const halo_ce::SaberCamera*);
using DrawFn=void(__fastcall*)(uintptr_t,const float*,uintptr_t);
struct Hook { void* target{};void* original{};bool enabled{}; };
Hook projection,draw;
HMODULE retained{};
uintptr_t base{};
std::atomic<uint32_t> generation{},callbacks{};
std::atomic<bool> installed{},active{},retiring{};
std::atomic<uint64_t> clipped{},offscreen{},passed{},unproven{},exceptions{};
std::atomic<uint64_t> userSuppressed{};
uint32_t rejectedGeneration{};
uint64_t lastReport{};
bool cleanupReported{};
enum class InstallResult { StockFallback, CleanupRequired, Installed };
struct Scope { uintptr_t effect{};const halo_ce::SaberCamera* camera{};uint32_t generation{};int32_t player{}; };
thread_local const Scope* scope{};
template<class T> bool Read(uintptr_t address,T& out) noexcept
{
    if (!address) return false;
    __try { std::memcpy(&out,reinterpret_cast<const void*>(address),sizeof(T));return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
#if HALOMCCVR_DEV_DIAGNOSTICS
// DEV BUILDS ONLY: which flare draws reach the screen, so a streak can be pinned to one. dev-ce-flares.txt beside
// the DLL holds a mask of draws to hide (1 = the guarded draw from 0x44718a, 2 = from 0x4472eb, 4 = any draw the
// guard does not own); the 2 s report adds counts per path, the first unowned callers and the flare drawn farthest
// from the centre of its eye.
namespace dev
{
enum Path : uint32_t { OwnedA,OwnedB,Unowned,PathCount };
std::atomic<uint32_t> hide{};
std::atomic<uint64_t> draws[PathCount]{},hidden[PathCount]{};
std::atomic<uintptr_t> unownedCallers[4]{};
std::atomic_flag lock=ATOMIC_FLAG_INIT;
struct Extreme { double radius{-1};float x{},y{},depth{},width{},height{};uintptr_t effect{};uint32_t path{}; } extreme;
uint64_t lastFileCheck{};
bool Hide(Path path) noexcept
{
    draws[path].fetch_add(1,std::memory_order_relaxed);
    if (!(hide.load(std::memory_order_relaxed)&(1u<<path))) return false;
    hidden[path].fetch_add(1,std::memory_order_relaxed);
    return true;
}
void NoteUnownedCaller(uintptr_t offset) noexcept
{
    for (auto& slot:unownedCallers)
    {
        uintptr_t seen=0;
        if (slot.compare_exchange_strong(seen,offset,std::memory_order_relaxed)||seen==offset) return;
    }
}
void NoteOwned(Path path,const halo_ce::SaberCamera& camera,halo_ce::Vec3 light,const float (&screen)[2],uintptr_t effect) noexcept
{
    const auto* pose=camera.pose.matrix;
    const halo_ce::Vec3 position{pose[12],pose[13],pose[14]},forward{pose[8],pose[9],pose[10]};
    const float width=camera.viewportWidth,height=camera.viewportHeight;
    if (!(width>=1)) return;
    const double x=(double(screen[0])-width*.5)/width,y=(double(screen[1])-height*.5)/width,radius=x*x+y*y;
    if (!std::isfinite(radius)) return;
    while (lock.test_and_set(std::memory_order_acquire)) {}
    if (radius>extreme.radius)
        extreme={radius,screen[0],screen[1],halo_ce::Dot(light-position,forward),width,height,effect,path};
    lock.clear(std::memory_order_release);
}
void ReadHideFile() noexcept
{
    const uint64_t now=GetTickCount64();
    if (now-lastFileCheck<1000) return;
    lastFileCheck=now;
    wchar_t path[MAX_PATH]{};HMODULE self{};unsigned mask=0;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&ReadHideFile),&self)&&GetModuleFileNameW(self,path,MAX_PATH))
    {
        if (wchar_t* slash=wcsrchr(path,L'\\')) wcscpy_s(slash+1,path+MAX_PATH-(slash+1),L"dev-ce-flares.txt");
        FILE* f=nullptr;
        if (_wfopen_s(&f,path,L"r")==0&&f) { if (fscanf_s(f,"%u",&mask)!=1) mask=0;fclose(f); }
    }
    mask&=7;
    if (mask!=hide.exchange(mask)) LOG("DEV: CE flares hidden mask=%u (1 = 0x44718a, 2 = 0x4472eb, 4 = unowned)",mask);
}
void ReportWindow() noexcept
{
    while (lock.test_and_set(std::memory_order_acquire)) {}
    const Extreme e=extreme;extreme={};
    lock.clear(std::memory_order_release);
    uint64_t d[PathCount],h[PathCount];
    for (uint32_t i=0;i<PathCount;++i) { d[i]=draws[i].exchange(0);h[i]=hidden[i].exchange(0); }
    LOG("DEV: CE flare draws in 2 s: 0x44718a %llu (hidden %llu), 0x4472eb %llu (hidden %llu), unowned %llu (hidden %llu); "
        "unowned callers +0x%llx +0x%llx +0x%llx +0x%llx; farthest owned: path %u effect %p at (%.0f, %.0f) of %.0fx%.0f, %.2f widths off centre, depth %.3f",
        d[OwnedA],h[OwnedA],d[OwnedB],h[OwnedB],d[Unowned],h[Unowned],
        static_cast<unsigned long long>(unownedCallers[0].load()),static_cast<unsigned long long>(unownedCallers[1].load()),
        static_cast<unsigned long long>(unownedCallers[2].load()),static_cast<unsigned long long>(unownedCallers[3].load()),
        e.path,reinterpret_cast<void*>(e.effect),e.x,e.y,e.width,e.height,e.radius>0?std::sqrt(e.radius):0.,e.depth);
}
}
#endif
bool Current() noexcept
{
    return installed.load(std::memory_order_acquire)&&active.load(std::memory_order_acquire)&&
        !retiring.load(std::memory_order_acquire)&&TitleAdapter_GetActiveTitle()==GameTitle::HaloCE&&
        TitleAdapter_GetGeneration(GameTitle::HaloCE)==generation.load(std::memory_order_acquire);
}
void ProjectionBody(uintptr_t effect,uint64_t opaque,const halo_ce::SaberCamera* camera,uintptr_t caller)
{
    const auto* previous=scope;Scope owned{};halo_ce::Tracking tracking{};
    scope=nullptr;
    if (caller==base+0x45109b&&Current()&&HaloCE_GetAnniversaryEyeTracking(camera,tracking)&&
        tracking.generation==generation.load(std::memory_order_acquire)&&
        Read(reinterpret_cast<uintptr_t>(camera)+0x220,owned.player)&&owned.player==0)
    { owned.effect=effect;owned.camera=camera;owned.generation=tracking.generation;scope=&owned; }
    __try { reinterpret_cast<ProjectionFn>(projection.original)(effect,opaque,camera); }
    __finally { scope=previous; }
}
__declspec(noinline) void ProjectionDispatch(uintptr_t effect,uint64_t opaque,const halo_ce::SaberCamera* camera,uintptr_t caller)
{
    callbacks.fetch_add(1,std::memory_order_acq_rel);
    __try { ProjectionBody(effect,opaque,camera,caller); }
    __finally
    {
        if (AbnormalTermination()) exceptions.fetch_add(1,std::memory_order_relaxed);
        callbacks.fetch_sub(1,std::memory_order_release);
    }
}
__declspec(noinline) void __fastcall ProjectionHook(uintptr_t effect,uint64_t opaque,const halo_ce::SaberCamera* camera)
{
    const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    // Own SEH in the actual entry: a tail-jump wrapper has no x64 unwind
    // record and cannot satisfy the cold thread-range retirement proof.
    callbacks.fetch_add(1,std::memory_order_acq_rel);
    __try { ProjectionBody(effect,opaque,camera,caller); }
    __finally
    {
        if (AbnormalTermination()) exceptions.fetch_add(1,std::memory_order_relaxed);
        callbacks.fetch_sub(1,std::memory_order_release);
    }
}
void DrawBody(uintptr_t effect,const float* point,uintptr_t record,uintptr_t caller)
{
        const auto* owned=scope;halo_ce::Tracking tracking{};[[maybe_unused]] bool guarded=false;
        if (owned&&Current()&&effect==owned->effect&&effect<=UINTPTR_MAX-0xc0&&
            owned->generation==generation.load(std::memory_order_acquire)&&
            (caller==base+0x44718a||caller==base+0x4472eb)&&
            record==effect+(caller==base+0x44718a?0x70:0x98)&&
            HaloCE_GetAnniversaryEyeTracking(owned->camera,tracking)&&
            tracking.generation==owned->generation&&Current())
        {
            guarded=true;
#if HALOMCCVR_DEV_DIAGNOSTICS
            const auto path=caller==base+0x44718a?dev::OwnedA:dev::OwnedB;
            if (dev::Hide(path)) return;
#endif
            // The setting is frozen with the owned stereo pair, so both eyes
            // see the same choice even if F1 changes it during preparation.
            // This is the proven native flare draw only, not world shading.
            if(tracking.disableAnniversaryLensFlares)
            { userSuppressed.fetch_add(1,std::memory_order_relaxed);return; }
            halo_ce::SaberCamera camera{};halo_ce::Vec3 light{};float screen[2]{};
            if (Read(reinterpret_cast<uintptr_t>(owned->camera),camera)&&
                Read(record,light)&&Read(reinterpret_cast<uintptr_t>(point),screen))
            {
                const auto disposition=halo_ce::ClassifySaberFlare(camera,light,screen);
                if (disposition==halo_ce::SaberFlareDisposition::Clipped)
                { clipped.fetch_add(1,std::memory_order_relaxed);return; }
                if (disposition==halo_ce::SaberFlareDisposition::Offscreen)
                { offscreen.fetch_add(1,std::memory_order_relaxed);return; }
                if (disposition==halo_ce::SaberFlareDisposition::Visible)
                {
                    passed.fetch_add(1,std::memory_order_relaxed);
#if HALOMCCVR_DEV_DIAGNOSTICS
                    dev::NoteOwned(path,camera,light,screen,effect);
#endif
                }
                else unproven.fetch_add(1,std::memory_order_relaxed);
            }
            else unproven.fetch_add(1,std::memory_order_relaxed);
        }
#if HALOMCCVR_DEV_DIAGNOSTICS
        if (!guarded) { dev::NoteUnownedCaller(caller-base);if (dev::Hide(dev::Unowned)) return; }
#endif
        reinterpret_cast<DrawFn>(draw.original)(effect,point,record);
}
__declspec(noinline) void DrawDispatch(uintptr_t effect,const float* point,uintptr_t record,uintptr_t caller)
{
    callbacks.fetch_add(1,std::memory_order_acq_rel);
    __try { DrawBody(effect,point,record,caller); }
    __finally { callbacks.fetch_sub(1,std::memory_order_release); }
}
__declspec(noinline) void __fastcall DrawHook(uintptr_t effect,const float* point,uintptr_t record)
{
    const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    callbacks.fetch_add(1,std::memory_order_acq_rel);
    __try { DrawBody(effect,point,record,caller); }
    __finally { callbacks.fetch_sub(1,std::memory_order_release); }
}
bool PendingCleanup(const char* operation,int status=0) noexcept
{
    if (!cleanupReported)
    {
        LOG("CE flare guard stock fallback: %s status=%d; retaining only this optional feature until cleanup can complete",operation,status);
        cleanupReported=true;
    }
    return false;
}
bool Remove() noexcept
{
    active=false;installed=false;retiring=true;
    for (auto* hook:{&projection,&draw}) if (hook->enabled)
    {
        const auto status=MCCVR_DisableHookForRetirement(hook->target);
        if (status!=MH_OK&&status!=MH_ERROR_DISABLED) return PendingCleanup("disable",status);
        hook->enabled=false;
    }
    const void* functions[]{reinterpret_cast<void*>(&ProjectionHook),reinterpret_cast<void*>(&DrawHook),
        reinterpret_cast<void*>(&ProjectionDispatch),reinterpret_cast<void*>(&DrawDispatch)};
    const void* originals[]{projection.original,draw.original,nullptr,nullptr};
    if (!WaitForNativeDetourQuiescence(functions,originals,4,callbacks)) return PendingCleanup("quiescence");
    for (auto* hook:{&projection,&draw}) if (hook->target)
    {
        const auto status=MH_RemoveHook(hook->target);
        if (status!=MH_OK&&status!=MH_ERROR_NOT_CREATED) return PendingCleanup("remove",status);
        *hook={};
    }
    if (callbacks.load()) return PendingCleanup("callbacks");
    base=0;generation=0;
    if (retained) { FreeLibrary(retained);retained=nullptr; }
    retiring=false;cleanupReported=false;return true;
}
InstallResult StockFallback() noexcept
{ return Remove()?InstallResult::StockFallback:InstallResult::CleanupRequired; }
InstallResult Install(uintptr_t module,size_t size,uint32_t gen) noexcept
{
    const char* failure=nullptr;
    const halo_ce::NativeContractSet contracts{halo_ce::contract::flare_guard::entries,
        halo_ce::contract::flare_guard::witnesses,halo_ce::contract::flare_guard::relatives,
        halo_ce::contract::flare_guard::pointers};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(module),&retained))
    { LOG("CE flare guard stock fallback: module lifetime pin unavailable");return InstallResult::StockFallback; }
    base=module;generation=gen;retiring=false;
    if (!halo_ce::VerifyNativeFeatureBindings(module,size,gen,contracts,failure))
    { LOG("CE flare guard stock fallback: %s",failure?failure:"binding failure");return StockFallback(); }
    const uintptr_t addresses[]{module+halo_ce::contract::flare_guard::flare_project,
        module+halo_ce::contract::flare_guard::flare_draw};
    void* detours[]{reinterpret_cast<void*>(&ProjectionHook),reinterpret_cast<void*>(&DrawHook)};
    Hook* hooks[]{&projection,&draw};
    for (size_t i=0;i<2;++i)
    {
        auto& hook=*hooks[i];hook.target=reinterpret_cast<void*>(addresses[i]);
        auto status=MH_CreateHook(hook.target,detours[i],&hook.original);
        if (status!=MH_OK)
        { hook={};LOG("CE flare guard stock fallback: create status %d",status);return StockFallback(); }
        status=MH_EnableHook(hook.target);
        if (status!=MH_OK)
        { LOG("CE flare guard stock fallback: enable status %d",status);return StockFallback(); }
        hook.enabled=true;
    }
    installed=true;
    LOG("CE flare guard installed: Anniversary primary eyes clip near-plane and residual offscreen halos beyond the native .8-width envelope; native lighting and visible flares preserved");
    return InstallResult::Installed;
}
void Poll(uintptr_t module,size_t size,uint32_t gen,bool isActive) noexcept
{
    active.store(isActive,std::memory_order_release);
    if (retained&&(!isActive||module!=base||gen!=generation.load()||retiring.load()))
        if (!Remove()) return;
    if (!isActive||!module||!gen) return;
    active.store(true,std::memory_order_release);
    if (!installed.load()&&gen!=rejectedGeneration)
        if (Install(module,size,gen)!=InstallResult::Installed) rejectedGeneration=gen;
    const uint64_t now=GetTickCount64();
    if (now-lastReport>=2000)
    {
        lastReport=now;
        LOG("CE flare guard gen=%u installed=%d clipped=%llu offscreen=%llu visible=%llu unproven=%llu exceptions=%llu userSuppressed=%llu",
            gen,installed.load(),clipped.load(),offscreen.load(),passed.load(),unproven.load(),exceptions.load(),userSuppressed.load());
    }
#if HALOMCCVR_DEV_DIAGNOSTICS
    dev::ReadHideFile();
    if (now==lastReport) dev::ReportWindow();
#endif
}
}
