#include "../src/common/odst_modded_fp_logic.h"
#include "../src/common/odst_mythic_layout.h"
#include "../src/common/odst_elite_layout.h"
#include <atomic>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
static unsigned checks=0;
static void Check(bool ok,const char* message)
{ ++checks; if(!ok){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);} }
#include "odst_modded_fp_assets.inl"
// Engine boundaries are deterministic stubs; the shipping capture, cache,
// palette admission and rollback code below are compiled without rewriting.
struct BoneMatrix { float value=0; };
struct FpInterpolationContext {
    const BoneMatrix* source=nullptr; uint32_t generation=0;
    int count=0,player=-1,slot=-1,wrist=-1,lWrist=-1,elbow=-1,lElbow=-1;
    int shoulder=-1,lShoulder=-1,cameraControl=-1,heldObjectStart=-1;
    uint64_t wristDescendants=0,lWristDescendants=0;
    FpHeldSources heldSources{}; uint32_t muzzleUnit=UINT32_MAX,muzzleWeapon=UINT32_MAX;
    bool valid=false;
};
static FpInterpolationContext g_fpInterpolationContexts[2];
static BoneMatrix g_fpUnmodifiedInterpolations[2][64];
static std::atomic<uint32_t> g_odstRuntimeGeneration{7};
struct Scope { bool armed=true; struct Tracking {uint64_t serial=1,referenceEpoch=1;} anatomicalTracking; };
static Scope g_fpStereoSolveScope;
enum class GameTitle {Halo3ODST};
static const Asset* active=nullptr;
static bool readable=true,markerSuccess=true;
int OdstReadFpModelIdentity(uint16_t tag,uint32_t& checksum)
{ checksum=tag==1?active->checksum:0; return tag==1?active->bodyCount:active->modelCount; }
static bool SafeReadBytes(const void* p,void* q,size_t n)
{ if(!p||!readable)return false;std::memcpy(q,p,n);return true; }
static bool OdstCaptureMuzzleOwner(int,int,uint32_t&,uint32_t&) {return false;}
static void VR_ObserveSecondaryWeaponPresentation(GameTitle,uint32_t) {}
static int barrelMeasurements=0,barrelResets=0,lastBarrelWrist=-1;
static void OdstResetAuthoredBarrel(int) {++barrelResets;}
static void MeasureOdstAuthoredBarrel(int,const BoneMatrix*,int wrist)
{++barrelMeasurements;lastBarrelWrist=wrist;}
static bool BuildOdstCenterFpRoot(int,BoneMatrix&) {return true;}
static bool ApplyControllerToMarkerBonesFromRoot(BoneMatrix,BoneMatrix* bones,int count,int,int,bool)
{if(!markerSuccess)return false;for(int i=0;i<count;++i)bones[i].value+=100;return true;}
static void PublishOdstFpLayoutSelfCheck(int,int,bool,const OdstFpSkeletonLayout&,int=0,uint32_t=0) {}
#define ODST_MODDED_FP_TESTING 1
#include "../src/dll/odst_modded_fp.inl"
#undef ODST_MODDED_FP_TESTING
#include "../src/dll/odst_mythic_layout.inl"
static uint64_t Descendants(const Asset& a,int root)
{
    uint64_t mask=0;
    for(int i=0;i<a.bodyCount;++i)
        for(int n=a.body[i],depth=0;n>=0&&depth<a.count;++depth)
        { if(n==a.body[root]){mask|=uint64_t{1}<<a.body[i];break;}n=a.parents[n]; }
    return mask;
}
static FpInterpolationContext Capture(BoneMatrix* bones,int count,int slot=0,int id=42)
{
    g_fpInterpolationContexts[slot]={};
    OdstCaptureMappedInterpolation(0,id,slot,true,&bones,&count,false,UINT32_MAX,UINT32_MAX);
    return g_fpInterpolationContexts[slot];
}
int main()
{
    for(const auto& a:assets)
    {
        active=&a;readable=true;markerSuccess=true;g_odstRuntimeGeneration.store(7);
        OdstMappedFpLayout layout{};
        const auto body=std::span<const int32_t>{a.body.data(),size_t(a.bodyCount)};
        const auto model=std::span<const int32_t>{a.model.data(),size_t(a.modelCount)};
        OdstMappedFpLayout mythic{};
        const bool early=ResolveOdstMythicEarlyCamera(a.checksum,body,a.count,mythic);
        Check(early==(a.checksum==0x14020C1Fu && a.camera==1),"only evidenced early-camera Mythic family selected");
        OdstMappedFpLayout elite{};
        const bool isElite=ResolveOdstEliteLayout(a.checksum,body,a.count,elite);
        Check(isElite==(a.checksum==0x10000E0Bu && a.camera==a.count-1),"Elite identity independent of human rig");
        BoneMatrix original[64]{};
        FpInterpolationContext direct{};direct.valid=true;direct.slot=0;direct.count=a.count;
        direct.generation=7;direct.source=original;direct.wrist=a.count<39?-1:6;direct.lWrist=5;
        for(int i=0;i<a.count;++i){original[i].value=999;g_fpUnmodifiedInterpolations[0][i].value=float(i);}
        g_odstMythicReceipts[0]={};
        g_fpStereoSolveScope.armed=true;
        OdstPrepareMythicInterpolation(0,42,0,direct);
        const int beforeReset=barrelResets;
        OdstCorrectMythicBodyPalette(1,a.body.data(),direct);
        if(isElite) {
            const auto primaryReceipt=g_odstMythicReceipts[0];
            const int savedMeasurements=barrelMeasurements,savedResets=barrelResets,savedWrist=lastBarrelWrist;
            std::array<int32_t,31> partial{};
            for(int i=0;i<31;++i)partial[i]=(i==1 || i==3 || i==5 ||
                (i>=7 && i<=10) || (i>=15 && i<=18) || (i>=23 && i<=26))?i:-1;
            OdstMappedFpLayout secondary{};
            Check(ResolveOdstEliteSecondaryLayout(a.checksum,partial,a.count,secondary),"native secondary Elite map admitted");
            FpInterpolationContext offhand{};offhand.valid=true;offhand.slot=1;
            offhand.generation=7;offhand.count=a.count;offhand.source=original;
            for(int i=0;i<a.count;++i)g_fpUnmodifiedInterpolations[1][i].value=float(i);
            g_odstMythicReceipts[1]={};
            OdstPrepareMythicInterpolation(0,99,1,offhand);
            OdstCorrectMythicBodyPalette(1,partial.data(),offhand);
            Check(offhand.lWrist==5 && offhand.cameraControl==a.camera,"secondary runtime uses left wrist and actual camera");
            Check(offhand.lWristDescendants==Descendants(a,5),"secondary four-finger mask");
            for(int i=0;i<a.modelCount;++i)
                Check((offhand.wristDescendants&(uint64_t{1}<<a.model[i]))!=0,"secondary gun follows dual carrier path");
            Check(g_odstMythicReceipts[1].valid,"secondary receipt retained");
            FpInterpolationContext nextLeft{};nextLeft.generation=7;nextLeft.count=a.count;
            OdstPrepareMythicInterpolation(0,99,1,nextLeft);
            Check(nextLeft.lWrist==5 && nextLeft.cameraControl==a.camera,"secondary reinterpolation retains mapping");
            auto primary=offhand;primary.slot=0;primary.wrist=-1;
            OdstCorrectMythicBodyPalette(1,partial.data(),primary);
            Check(primary.wrist==-1,"partial map never admitted to primary slot");
            partial[5]=-1;
            Check(!ResolveOdstEliteSecondaryLayout(a.checksum,partial,a.count,secondary),"missing left wrist rejected");
            g_odstMythicReceipts[0]=primaryReceipt;
            barrelMeasurements=savedMeasurements;barrelResets=savedResets;lastBarrelWrist=savedWrist;
            auto invalid=a.body;invalid[0]=-1;
            OdstMappedFpLayout rejected{};
            Check(!ResolveOdstEliteLayout(a.checksum,std::span<const int32_t>{invalid.data(),31},a.count,rejected),"partial Elite body not admitted");
            Check(!ResolveOdstEliteLayout(0x14020C1F,body,a.count,rejected),"human identity cannot select Elite solver");
        }
        if(early || isElite) {
            Check(direct.wrist==a.body[6] && direct.lWrist==a.body[5] && direct.cameraControl==a.camera,"live corrected role indices");
            Check(direct.lWristDescendants==Descendants(a,5),"live left finger ownership");
            Check((direct.lWristDescendants&direct.wristDescendants)==0,"hands never share finger or gun sources");
            for(int i=0;i<a.modelCount;++i)
                Check((direct.wristDescendants&(uint64_t{1}<<a.model[i]))!=0,"entire gun carried");
            Check(!(direct.wristDescendants&(uint64_t{1}<<a.camera)),"camera not carried as gun");
            Check(original[0].value==100,"marker rebuilt from untouched graph");
            Check(g_fpInterpolationContexts[0].valid && g_fpInterpolationContexts[0].wrist==a.body[6],"gun palette retains corrected receipt");
            Check(barrelResets==beforeReset+1 && lastBarrelWrist==a.body[6],"discard wrong-wrist calibration on first discovery");
            // Native code may interpolate again between body and gun palettes.
            // A fresh context must not put the weapon back on wrist 6 or hide
            // its last component as camera_control.
            FpInterpolationContext next=direct;next.wrist=6;next.lWrist=5;next.cameraControl=a.count-1;
            next.wristDescendants=0;next.lWristDescendants=0;
            OdstPrepareMythicInterpolation(0,42,0,next);
            Check(next.wrist==a.body[6] && next.lWrist==a.body[5] && next.cameraControl==a.camera,"new interpolation retains observed body roles");
            Check((next.wristDescendants&(uint64_t{1}<<a.model[a.modelCount-1]))!=0,"last gun component remains visible/carried");
            const int measured=barrelMeasurements;
            const float transformed=original[0].value;
            OdstCorrectMythicBodyPalette(1,a.body.data(),next);
            Check(barrelMeasurements==measured && original[0].value==transformed,"body palette does not double-calibrate or double-transform");
            const auto receipt=g_odstMythicReceipts[0];
            ++g_fpStereoSolveScope.anatomicalTracking.referenceEpoch;
            next.wrist=6;OdstPrepareMythicInterpolation(0,42,0,next);
            Check(next.wrist==6 && !g_odstMythicReceipts[0].valid,"reference epoch invalidates receipt");
            --g_fpStereoSolveScope.anatomicalTracking.referenceEpoch;
            g_odstMythicReceipts[0]=receipt;
            next.generation=8;OdstPrepareMythicInterpolation(0,42,0,next);
            Check(!g_odstMythicReceipts[0].valid,"runtime generation invalidates receipt");
            next.generation=7;g_odstMythicReceipts[0]=receipt;
            next.wrist=6;OdstPrepareMythicInterpolation(0,43,0,next);
            Check(next.wrist==6 && !g_odstMythicReceipts[0].valid,"weapon identity change returns to baseline");

        } else Check(original[0].value==999 && direct.wrist==(a.count<39?-1:6),"other layouts preserve baseline behavior");
        Check(ResolveOdstMappedFpLayout(a.checksum,body,a.count,layout),"actual ODST body admitted");
        Check(layout.skeleton.leftHandDescendants==Descendants(a,5),"own left fingers");
        Check(layout.skeleton.rightHandAndWeaponDescendants==Descendants(a,6),"own right fingers");
        if(a.bodyCount==31)
        {
            OdstFpSkeletonLayout old{};
            const bool oldAccepted=ComputeOdstFpSkeletonLayout(a.count,old);
            Check(a.count<39?!oldAccepted:oldAccepted,"reproduce old count gate");
            if(oldAccepted)Check(old.leftHandDescendants!=Descendants(a,5),"reproduce old Elite finger corruption");
        }
        uint64_t gunMask=0;
        Check(OdstCollectSourceMask(model,a.count,gunMask),"native gun map is injective");
        OdstFpLayoutEntry complete{};complete.key.count=a.count;
        complete.layout=layout;complete.heldMask=gunMask;OdstFpLayoutCache::Complete(complete);
        Check(complete.valid && complete.layout.skeleton.cameraControl==a.camera,"derive actual camera from both maps");
        layout=complete.layout;
        if(a.camera==1) {
            OdstFpSkeletonLayout old{};
            Check(ComputeOdstFpSkeletonLayout(a.count,old),"old code accepted early-camera Automag by count");
            Check(old.rightWrist!=layout.skeleton.rightWrist && old.leftWrist!=layout.skeleton.leftWrist,
                "reproduce Automag wrong wrist indices");
            Check(old.cameraControl!=a.camera,"reproduce last-node camera assumption");
        }
        FpHeldSources held{};
        Check(ResolveOdstHeldSources(layout,model,a.count,held),"actual gun mapping");
        for(int n=0;n<a.count;++n)
            Check(FpSourceIndexIsHeldObject(n,a.count,-1,held)==
                (std::find(model.begin(),model.end(),n)!=model.end()),"only native gun outputs carried/visible");
        // Permute every source except the separately reserved camera. Neither
        // hand ownership nor gun ownership may depend on prefix positions.
        auto perm=a.body,gun=a.model;
        for(int n=0;n<a.bodyCount;++n)perm[n]=a.count-1-perm[n];
        for(int n=0;n<a.modelCount;++n)gun[n]=a.count-1-gun[n];
        OdstMappedFpLayout reordered{};
        Check(ResolveOdstMappedFpLayout(a.checksum,{perm.data(),size_t(a.bodyCount)},a.count,reordered),"reordered body");
        Check(reordered.skeleton.rightWrist==perm[6],"mapped wrist");
        reordered.skeleton.cameraControl=a.count-1-a.camera;
        Check(ResolveOdstHeldSources(reordered,{gun.data(),size_t(a.modelCount)},a.count,held),"reordered gun");
        Check(!ResolveOdstMappedFpLayout(0,body,a.count,reordered),"unknown body rejected");
        perm[1]=perm[0];
        Check(!ResolveOdstMappedFpLayout(a.checksum,{perm.data(),size_t(a.bodyCount)},a.count,reordered),"duplicate body rejected");
        Check(!ResolveOdstHeldSources(layout,body,a.count,held),"body cannot become gun");
        int32_t invalid[]{a.camera};
        Check(!ResolveOdstHeldSources(layout,invalid,a.count,held),"camera cannot become gun");
        invalid[0]=-2;
        Check(!ResolveOdstHeldSources(layout,invalid,a.count,held),"invalid sentinel rejected");
        invalid[0]=-1;
        Check(!ResolveOdstHeldSources(layout,invalid,a.count,held),"empty selection rejected");
        Check(!ResolveOdstMappedFpLayout(a.checksum,body,65,reordered),"bounded graph");
        // Regression: the old hook consumed the interpolation on the first
        // palette. Exercise the real receipt selector without recapturing.
        for(bool gunFirst:{false,true}) {
            g_odstMappedFpCache={};g_fpStereoSolveScope={};readable=true;
            BoneMatrix shared[64]{};
            Capture(shared,a.count);
            for(int pass=0;pass<2;++pass) {
                const bool isGun=gunFirst==(pass==0);
                auto receipt=OdstMappedPaletteContext(shared);
                Check(receipt.valid,"one interpolation feeds both native palettes");
                Check(!OdstResolveMappedPalette(isGun?2:1,isGun?a.model.data():a.body.data(),receipt),"learning pair remains stock in either order");
            }
            ++g_fpStereoSolveScope.anatomicalTracking.serial;
            Capture(shared,a.count);
            for(int pass=0;pass<2;++pass) {
                const bool isGun=gunFirst==(pass==0);
                auto receipt=OdstMappedPaletteContext(shared);
                Check(OdstResolveMappedPalette(isGun?2:1,isGun?a.model.data():a.body.data(),receipt),"both palettes accepted from one interpolation");
                Check(receipt.cameraControl==a.camera,"runtime camera is the asset camera, not last node");
            }
            BoneMatrix foreign[64]{};
            Check(!OdstMappedPaletteContext(foreign).valid,"unrelated source cannot borrow receipt");
        }
        OdstFpLayoutEntry ambiguous=complete;
        ambiguous.heldMask=0;OdstFpLayoutCache::Complete(ambiguous);
        Check(!ambiguous.valid,"body alone cannot authorize a guessed camera");
        ambiguous=complete;ambiguous.heldMask|=uint64_t{1}<<a.body[0];
        OdstFpLayoutCache::Complete(ambiguous);
        Check(!ambiguous.valid,"overlapping body and gun rejected");
        ambiguous=complete;ambiguous.heldMask&=ambiguous.heldMask-1;
        OdstFpLayoutCache::Complete(ambiguous);
        Check(!ambiguous.valid,"multiple unmapped nodes cannot identify camera");
        for(int slot=0;slot<2;++slot)
        {
            g_odstMappedFpCache={};g_fpStereoSolveScope={};readable=true;
            BoneMatrix bones[64]{};
            auto context=Capture(bones,a.count,slot);
            Check(!OdstResolveMappedPalette(1,a.body.data(),context),"first body learns stock");
            context=Capture(bones,a.count,slot);
            Check(!OdstResolveMappedPalette(2,a.model.data(),context),"same-pair gun remains stock");
            context=Capture(bones,a.count,slot);
            Check(!OdstResolveMappedPalette(1,a.body.data(),context),"second eye cannot adopt newly learned body");
            ++g_fpStereoSolveScope.anatomicalTracking.serial;
            context=Capture(bones,a.count,slot);
            Check(OdstResolveMappedPalette(1,a.body.data(),context),"next-pair body resolves");
            Check(context.wrist==a.body[6] && context.lWrist==a.body[5],"runtime anchor assignment");
            Check(context.heldSources.explicitMap&&!context.heldSources.bits[0],"body does not own gun outputs");
            std::fill(std::begin(bones),std::end(bones),BoneMatrix{});
            context=Capture(bones,a.count,slot);
            Check(OdstResolveMappedPalette(2,a.model.data(),context),"next-pair gun resolves");
            Check(context.heldSources.bits[0]!=0,"runtime gun selection");
            Check(bones[0].value==100,"marker transformed");
            std::fill(std::begin(bones),std::end(bones),BoneMatrix{});
            context=Capture(bones,a.count,slot);readable=false;
            Check(!OdstResolveMappedPalette(2,a.model.data(),context),"unreadable map rejects");
            Check(bones[0].value==0,"rejection restores native graph");readable=true;
            ++g_fpStereoSolveScope.anatomicalTracking.serial;
            context=Capture(bones,a.count,slot);
            Check(!OdstResolveMappedPalette(2,a.model.data(),context),"rejected layout retired next pair");
            context=Capture(bones,a.count,slot);OdstResolveMappedPalette(1,a.body.data(),context);
            ++g_fpStereoSolveScope.anatomicalTracking.serial;
            context=Capture(bones,a.count,slot,99);
            Check(!OdstResolveMappedPalette(2,a.model.data(),context),"interpolation identity isolated");
            // A different body remap with the same count/id must be observed
            // afresh, and speculative marker writes must be rolled back.
            std::fill(std::begin(bones),std::end(bones),BoneMatrix{});
            context=Capture(bones,a.count,slot);
            auto changed=a.body;std::swap(changed[1],changed[2]);
            Check(!OdstResolveMappedPalette(1,changed.data(),context),"same-count body change rejects");
            Check(bones[0].value==0,"changed body restores markers");
            ++g_fpStereoSolveScope.anatomicalTracking.serial;
            context=Capture(bones,a.count,slot);
            Check(OdstResolveMappedPalette(1,changed.data(),context),"changed body learned next pair");
            Check(context.shoulder==changed[2]&&context.lShoulder==changed[1],"changed anchors applied");
            ++g_odstRuntimeGeneration;
            context=Capture(bones,a.count,slot);
            Check(!OdstResolveMappedPalette(2,a.model.data(),context),"generation retires layout");
            ++g_fpStereoSolveScope.anatomicalTracking.referenceEpoch;
            context=Capture(bones,a.count,slot);
            Check(!OdstResolveMappedPalette(2,a.model.data(),context),"recenter retires layout");
        }
    }
    std::printf("ODST modded FP: %u checks across %zu asset/body combinations passed\n",checks,std::size(assets));
}
