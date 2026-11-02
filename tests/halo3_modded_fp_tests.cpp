#include "../src/common/halo3_held_palette_logic.h"
#include "../src/common/legacy_runtime_weapon_bounds.h"
#include "../src/common/reach_render_logic.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static unsigned checks{};
static void Check(bool value, const char* what)
{
    ++checks;
    if (!value) { std::fprintf(stderr,"FAIL: %s\n",what); std::exit(1); }
}
enum class GameTitle { Halo3, Halo3ODST, HaloReach };
struct BoneMatrix { float unused{}; };
struct FpInterpolationContext {
    const BoneMatrix* source{};
    uint32_t generation{};
    int slot{}, count{}, cameraControl{}, wrist{}, lWrist{};
    bool valid{};
    FpHeldSources heldSources{};
};
struct ReachContext { const BoneMatrix* source{}; uint32_t generation{};
    int liveSourceCount{}; ReachFpBodyLayout layout{}; };
static FpInterpolationContext g_fpInterpolationContexts[2];
static ReachContext g_reachFpInterpolations[2];
static std::atomic<uint32_t> g_halo3RuntimeGeneration{7};
static int nodeCount{};
static bool readable=true;
static uint64_t now=1000;
static int LegacyAnatomicalRenderNodeCount(GameTitle, uint16_t) { return nodeCount; }
static bool ReachResolvePaletteNodeCount(uint16_t, int&) { return false; }
static bool SafeReadBytes(const void* from, void* to, size_t bytes)
{ if (!readable || !from) return false; std::memcpy(to,from,bytes); return true; }
static uint64_t GetTickCount64() { return now; }
#include "halo3_modded_fp_fixture.inl"
#include "halo3_modded_fp_assets.inl"

static uint64_t Descendants(const Asset& a, int root)
{
    uint64_t result{};
    for (int i=0;i<a.count;++i)
        for (int node=i, depth=0;node>=0 && depth<a.count;++depth)
        {
            if (node==root) { result|=uint64_t{1}<<i; break; }
            node=a.parents[node];
        }
    return result;
}

int main()
{
    BoneMatrix source[2][64]{};
    for (const auto& a:assets)
    {
        const auto right=Descendants(a,a.right), left=Descendants(a,a.left);
        const bool detached=a.model[0]==37;
        for (int slot:{0,1})
        {
            auto& context=g_fpInterpolationContexts[slot];
            context={source[slot],7,slot,a.count,a.camera,a.right,a.left,true,{}};
            // Learn the actual contiguous body prefix for the detached models.
            // The control graph has an early camera and intentionally retains
            // the pre-existing wrist-subtree path, without false admission.
            int32_t body[37]{};
            for (int i=0;i<37;++i) body[i]=i;
            nodeCount=37;
            Check(!Halo3SelectHeldPaletteSources(1,body,context),"Body is never selected as a weapon");
            nodeCount=a.modelCount;
            Check(Halo3SelectHeldPaletteSources(2,a.model.data(),context)==detached,
                  "Production classification/selection admits detached weapons without a name list");
            for (int i=0;i<a.count;++i)
            {
                bool model=false;
                for (int n=0;n<a.modelCount;++n) model|=a.model[n]==i;
                const bool oldVisible=bool((right|left)&(uint64_t{1}<<i));
                if (model && detached)
                    Check(!oldVisible,"Old floating-hands rule reproduces every disappearing weapon output");
                Check(Halo3FpSourceVisible(i,a.count,right|left,context.heldSources)==(oldVisible||model),
                      "New visibility keeps all held parts and both hands, hiding other bones");
                Check(FpSourceIndexIsHeldObject(i,a.count,-1,context.heldSources)==(detached&&model),
                      "IK carry and scale select only actual detached model outputs");
            }
            auto stale=context; stale.generation=8;
            Check(!Halo3SelectHeldPaletteSources(2,a.model.data(),stale) && !stale.heldSources.explicitMap,
                  "Stale generation clears held selection");
            stale=context; stale.valid=false;
            Check(!Halo3SelectHeldPaletteSources(2,a.model.data(),stale),"Consumed/invalid transaction refused");
            readable=false;
            Check(!Halo3SelectHeldPaletteSources(2,a.model.data(),context) && !context.heldSources.explicitMap,
                  "Read failure cannot reuse previous weapon ownership");
            readable=true;
            if (detached)
            {
                now+=151;
                Check(!Halo3SelectHeldPaletteSources(2,a.model.data(),context),"Expired body observation is refused");
            }
        }
    }
    FpHeldSources held{};
    for (const std::array<int32_t,2> bad: {std::array<int32_t,2>{37,37},{37,42},{37,-2},{-1,-1},{37,41}})
        Check(!ResolveHalo3HeldPaletteSources(true,bad,42,41,held),"Malformed maps never acquire held ownership");
    const std::array<int32_t,3> optional{37,-1,40};
    Check(ResolveHalo3HeldPaletteSources(true,optional,42,41,held) && held.bits[0]==((1ull<<37)|(1ull<<40)),
          "Native optional unmapped output is ignored without inventing extra sources");
    Check(!ResolveHalo3HeldPaletteSources(false,optional,42,41,held) && !held.explicitMap,
          "Map-only evidence cannot bypass native classification");
    Check(!Halo3FpSourceVisible(-1,42,~0ull,held) && !Halo3FpSourceVisible(42,42,~0ull,held),
          "Visibility is bounded");
    const std::array<int32_t,1> last{63};
    Check(ResolveHalo3HeldPaletteSources(true,last,64,62,held) &&
          FpSourceIndexIsHeldObject(63,64,-1,held),"Highest mask bit is safe");
    Check(!ResolveHalo3HeldPaletteSources(true,last,65,62,held),"Existing H3 capacity is preserved");
    std::printf("Halo 3 modded FP: %u checks passed\n",checks);
}
