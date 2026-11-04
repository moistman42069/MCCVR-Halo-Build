#include "haloce_view_pair.h"
#include "haloce_surface_transfer.h"
#include "haloce_prepared_handoff.h"
#include "haloce_view_construction.h"
#include <cstdio>
#include <limits>
#include <thread>
#include <array>
#include <utility>
#include <fstream>

using namespace halo_ce;

// Offline bridge harness: the Python verifier supplies cameras produced by
// CE's actual instructions. Pass them through the production tracking helper,
// then return the private staged bytes for native rebuild/consumer emulation.
// This mode neither loads the engine DLL nor claims to render the game.
int NativeCameraAdapter(const char* input,const char* output)
{
    std::ifstream source(input,std::ios::binary);
    std::ofstream destination(output,std::ios::binary|std::ios::trunc);
    uint32_t count{};
    if (!source.read(reinterpret_cast<char*>(&count),sizeof(count))||!count||count>256||!destination) return 2;
    destination.write(reinterpret_cast<const char*>(&count),sizeof(count));
    Tracking tracking{}; tracking.serial=300; tracking.spaceEpoch=7; tracking.generation=5;
    tracking.headPosition={.21f,.13f,-.17f};
    tracking.headOrientation={0,.2588190451f,0,.9659258263f};
    for (int eye=0;eye<2;++eye)
    {
        tracking.eyes[eye].offset=Rotate(tracking.headOrientation,{eye?.0355f:-.0355f,0,0});
        tracking.eyes[eye].orientation=tracking.headOrientation;
        auto& fov=tracking.eyes[eye].fov;
        fov[0]=-.91f; fov[1]=.83f; fov[2]=.87f; fov[3]=-.98f;
    }
    Reference reference{}; reference.spaceEpoch=7; reference.generation=5;
    for (uint32_t record=0;record<count;++record)
    {
        SaberCamera camera{};
        if (!source.read(reinterpret_cast<char*>(&camera),sizeof(camera))) return 3;
        for (int eye=0;eye<2;++eye)
        {
            SaberCamera staged{}; Cover cover{};
            if (!StageSaberEye(camera,tracking,reference,eye,1.0f/3.048f,true,staged,cover)) return 4;
            destination.write(reinterpret_cast<const char*>(&staged),sizeof(staged));
            destination.write(reinterpret_cast<const char*>(&cover),sizeof(cover));
        }
    }
    if (source.peek()!=std::char_traits<char>::eof()||!destination) return 5;
    return 0;
}

int main(int argc,char** argv)
{
    if (argc==4&&std::strcmp(argv[1],"--native-camera-adapter")==0)
        return NativeCameraAdapter(argv[2],argv[3]);
    if (argc!=1) return 2;
    int failures=0;
    const auto check=[&](bool ok,const char* what) {
        if (!ok) { std::fprintf(stderr,"CE pair: %s\n",what); ++failures; }
    };
    const auto near=[](float a,float b) { return std::fabs(a-b)<0.0001f; };
    Camera source{};
    source.position={10,20,30}; source.forward={1,0,0}; source.up={0,0,1};
    source.verticalFov=1.2f; source.nearPlane=0.01f; source.farPlane=1000;
    source.viewport=source.window={0,0,1080,1920};
    SaberViewPair native{};
    native.flags=1; native.count=2;
    for (int eye=0;eye<2;++eye)
    {
        SaberView& view=native.views[eye];
        view.flags=eye?0x20b:0x10b; view.viewIndex=eye;
        view.native10[0]=0x5a;
        SaberCamera& camera=view.camera;
        BuildSaberPose(source,{},0,camera.pose);
        camera.viewportWidth=1920; camera.viewportHeight=1080;
        camera.verticalFovDegrees=source.verticalFov*57.29577951308232f;
        camera.horizontalFovDegrees=100;
        camera.nearPlane=source.nearPlane*kSaberUnitsPerNativeUnit;
        camera.farPlane=source.farPlane*kSaberUnitsPerNativeUnit;
        camera.derived160[100]=0xad;
    }
    const SaberViewPair savedNative=native;
    Tracking tracking{}; tracking.serial=200; tracking.spaceEpoch=5; tracking.generation=3;
    tracking.eyes[0].offset={-0.032f,0,0}; tracking.eyes[1].offset={0.032f,0,0};
    for (Eye& eye:tracking.eyes)
    { eye.fov[0]=-0.9f; eye.fov[1]=0.8f; eye.fov[2]=0.85f; eye.fov[3]=-0.95f; }
    Reference reference{}; reference.spaceEpoch=5; reference.generation=3;
    StagedViewPair staged{};
    int rebuilds=0;
    // A fixture for transaction ordering/failure propagation. This is not an
    // emulation or test of the native view/frustum/culling builders themselves.
    const auto rebuild=[&](SaberCamera& camera) {
        ++rebuilds; camera.view[0]=42; return true;
    };
    check(ValidNativePair(native),"native two-primary-view shape admitted");
    check(StageNativeViewPair(native,tracking,reference,0.33f,true,rebuild,staged)==
        PairStageResult::Staged&&rebuilds==2,"two private cameras rebuilt once each");
    check(staged.serial==200&&staged.generation==3&&staged.spaceEpoch==5,
        "prepared pair carries exact tracking identity");
    check(staged.cameras[0].view[0]==42&&staged.cameras[1].view[0]==42&&
        staged.cameras[0].derived160[100]==0xad,"rebuild result and opaque state retained");
    check(near(staged.cameras[0].pose.matrix[14]-staged.cameras[1].pose.matrix[14],
        -0.064f*0.33f*kSaberUnitsPerNativeUnit),"independent eye separation applied exactly once");
    check(std::memcmp(&native,&savedNative,sizeof(native))==0,
        "staging cannot mutate live cameras, native headers or count");
    const StagedViewPair saved=staged;
    // Model the native append's independent position table. The old sequence
    // appends stock origins, then changes only the camera records afterward.
    // Exercise a translated/rotated head, not only two solid-color eye copies.
    {
        ViewConstruction construction{};
        construction.tracking=tracking; construction.reference=reference;
        construction.tracking.headPosition={.4f,.2f,-.3f};
        construction.tracking.headOrientation={0,.258819f,0,.965926f};
        for (auto& eye:construction.tracking.eyes) eye.orientation=construction.tracking.headOrientation;
        construction.unitsPerMeter=.33f; construction.positional=true;
        construction.width=1920; construction.height=540;
        SaberViewPair builtViews=native;
        float origins[2][3]{};
        const auto nativeRebuild=[](SaberCamera&) { return true; };
        for (int eye=0;eye<2;++eye)
        {
            check(construction.PrepareEye(eye,native.views[eye].camera,nativeRebuild),
                "tracked input camera exists before native append");
            builtViews.views[eye].camera=construction.staged.cameras[eye];
            std::memcpy(origins[eye],builtViews.views[eye].camera.pose.matrix+12,sizeof(origins[eye]));
            builtViews.views[eye].camera.derived160[100]=static_cast<uint8_t>(0x70+eye);
        }
        // Native 454A7B/454A8D override the primary clips; ordinary secondary
        // 454C93 returns without the corresponding native-stereo override.
        builtViews.views[0].camera.nearPlane=.025f;
        builtViews.views[0].camera.farPlane=1000;
        builtViews.views[1].camera.nearPlane=.1f;
        builtViews.views[1].camera.farPlane=10000;
        StagedViewPair constructed{};
        check(construction.Finish(builtViews,nativeRebuild,constructed),
            "native pair finalizes without applying head tracking a second time");
        for (int eye=0;eye<2;++eye)
        {
            check(std::memcmp(origins[eye],constructed.cameras[eye].pose.matrix+12,sizeof(origins[eye]))==0,
                "native origin metadata describes exactly the final eye camera");
            check(std::memcmp(native.views[eye].camera.pose.matrix+12,origins[eye],sizeof(origins[eye]))!=0,
                "old append-then-patch sequence leaves a provably stale origin");
            check(constructed.cameras[eye].nearPlane==.025f&&constructed.cameras[eye].farPlane==1000&&
                constructed.cameras[eye].derived160[100]==0x70+eye,
                "both eyes use primary native clips while retaining per-eye native opaque data");
        }
        check(!construction.PrepareEye(1,native.views[1].camera,nativeRebuild),"duplicate append rejected");
        const auto complete=constructed;
        builtViews.views[1].camera.pose.matrix[12]+=1;
        check(!construction.Finish(builtViews,nativeRebuild,constructed)&&
            std::memcmp(&complete,&constructed,sizeof(complete))==0,"changed native eye never publishes a partial pair");
        construction.mask=0;
        check(!construction.PrepareEye(1,native.views[1].camera,nativeRebuild),"out-of-order right construction rejected");
        check(!construction.Finish(native,nativeRebuild,constructed),"missing construction cannot inherit a receipt");
    }
    {
        // AIM FRAME ZOOM: with the frame set before eye 0, both eyes are staged as the frame's, from the centre eye, at
        // the picture's angles in the raster's shape; the finished pair holds the rebuilt cameras to those angles.
        ViewConstruction construction{};
        construction.tracking=tracking; construction.reference=reference;
        construction.unitsPerMeter=.33f; construction.positional=true;
        construction.width=1920; construction.height=1080;
        Camera stock{},centre{};
        check(construction.CentreEye(native.views[0].camera,stock,centre)&&
            near(centre.position.x,stock.position.x)&&near(centre.position.y,stock.position.y)&&
            stock.viewport.right==1920&&stock.viewport.bottom==1080,
            "the centre eye stands between the eyes, from the stock camera at the raster's size");
        AimFrameEyes frame{};
        frame.tanY=0.15f; frame.tanX=0.15f*1920.0f/1080.0f; frame.widthDeg=50.0f;
        for (int eye=0;eye<2;++eye)
        { frame.position[eye]=centre.position; frame.forward[eye]={0,1,0}; frame.up[eye]={0,0,1}; }
        construction.aimFrame=true; construction.frame=frame; construction.centre=centre;
        SaberViewPair builtViews=native;
        const auto nativeRebuild=[](SaberCamera&) { return true; };
        for (int eye=0;eye<2;++eye)
        {
            check(construction.PrepareEye(eye,native.views[eye].camera,nativeRebuild),"a frame eye stages");
            builtViews.views[eye].camera=construction.staged.cameras[eye];
        }
        Camera staged{};
        check(NativeCameraFromSaber(construction.staged.cameras[0],staged)&&near(staged.forward.y,1.0f)&&
            near(staged.verticalFov,2*std::atan(0.15f))&&
            near(construction.staged.cameras[1].horizontalFovDegrees,2*std::atan(frame.tanX)*57.29577951308232f)&&
            near(construction.staged.covers[1].halfY,std::atan(0.15f))&&
            std::memcmp(&construction.staged.cameras[0].pose,&construction.staged.cameras[1].pose,sizeof(SaberPose))==0,
            "a flat frame draws one camera along the aim twice, at the picture's angles");
        StagedViewPair constructed{};
        check(construction.Finish(builtViews,nativeRebuild,constructed),"the frame pair finalizes");
        builtViews.views[1].camera.verticalFovDegrees+=1.0f;
        const auto widened=[](SaberCamera& camera) { camera.verticalFovDegrees+=1.0f; return true; };
        check(!construction.Finish(builtViews,widened,constructed),
            "a rebuilt camera that is not the picture asked for refuses the frame");
    }
    SaberViewPair fullDesktop=savedNative,rasterSource{};
    for (auto& view:fullDesktop.views)
    { view.camera.viewportWidth=2912; view.camera.viewportHeight=2100; }
    check(SelectNativeEyeRaster(fullDesktop,2912,1050,rasterSource)&&
        fullDesktop.views[0].camera.viewportHeight==2100,
        "logged half-height source selects a private raster without changing the desktop camera");
    StagedViewPair sourceSized{};
    check(StageNativeViewPair(rasterSource,tracking,reference,0.33f,true,rebuild,sourceSized)==PairStageResult::Staged&&
        sourceSized.cameras[0].viewportHeight==1050&&sourceSized.cameras[1].viewportHeight==1050&&
        near(std::tan(sourceSized.covers[0].halfX)/std::tan(sourceSized.covers[0].halfY),2912.0f/1050.0f),
        "eye projection aspect is rebuilt for 2912x1050 pixels rather than the 2912x2100 desktop");
    for (const auto dimensions:{std::pair{0u,1050u},std::pair{2912u,0u},std::pair{16385u,1050u}})
        check(!SelectNativeEyeRaster(fullDesktop,dimensions.first,dimensions.second,rasterSource),
            "invalid observed raster cannot change staged source cameras");
    tracking.eyes[1].orientation.w=std::numeric_limits<float>::quiet_NaN();
    rebuilds=0;
    check(StageNativeViewPair(native,tracking,reference,0.33f,true,rebuild,staged)==
        PairStageResult::InvalidTracking&&rebuilds==0&&
        std::memcmp(&staged,&saved,sizeof(saved))==0,
        "invalid right eye causes no native calls or partial publication");
    tracking.eyes[1].orientation={};
    rebuilds=0;
    check(StageNativeViewPair(native,tracking,reference,0.33f,true,
        [&](SaberCamera& camera) { camera.view[0]=99; return ++rebuilds!=2; },staged)==
        PairStageResult::RebuildFailed&&std::memcmp(&staged,&saved,sizeof(saved))==0,
        "right rebuild failure publishes neither eye");
    check(std::memcmp(&native,&savedNative,sizeof(native))==0,
        "failed rebuild leaves native pair byte-identical");
    check(StageNativeViewPair(native,tracking,reference,0.33f,true,
        [](SaberCamera& camera) { camera.verticalFovDegrees=1; return true; },staged)==
        PairStageResult::InvalidRebuiltCamera&&std::memcmp(&staged,&saved,sizeof(saved))==0,
        "native FOV disagreement rejected without stale cover publication");
    check(StageNativeViewPair(native,tracking,reference,0.33f,true,
        [](SaberCamera& camera) { camera.viewportWidth*=0.5f; return true; },staged)==
        PairStageResult::InvalidRebuiltCamera,"native raster disagreement rejected");
    check(StageNativeViewPair(native,tracking,reference,0.33f,true,
        [](SaberCamera& camera) { camera.pose.matrix[12]+=1; return true; },staged)==
        PairStageResult::InvalidRebuiltCamera,"native pose disagreement rejected");
    check(StageNativeViewPair(native,tracking,reference,0.33f,true,
        [](SaberCamera& camera) {
            camera.view[7]=std::numeric_limits<float>::quiet_NaN(); return true;
        },staged)==PairStageResult::InvalidRebuiltCamera,
        "non-finite native derived view cannot enter the eye pair");
    for (uint32_t count:{0u,1u,3u,0xffffffffu})
    {
        native.count=count;
        check(!ValidNativePair(native),"unexpected view count rejected");
    }
    native=savedNative; native.flags=3;
    check(!ValidNativePair(native),"native stereo convergence shift excluded");
    native=savedNative; native.views[1].viewIndex=0;
    check(!ValidNativePair(native),"duplicate native view identity rejected");
    native=savedNative; native.views[0].flags|=4;
    check(!ValidNativePair(native),"native non-primary camera flags rejected");
    native=savedNative; native.views[1].camera.pose.matrix[12]+=1;
    check(!ValidNativePair(native),"independent split-screen camera never replaced");
    native=savedNative; native.views[0].flags|=0x1000;
    check(ValidNativePair(native),"native zoom selection retained without changing flags");
    native=savedNative;
    // Exercise head pitch/roll/yaw, asymmetric optics and translated reference
    // across non-square rasters. Compare the separation's length to real IPD,
    // rather than duplicating the implementation's coordinate calculations.
    for (int axis=0;axis<3;++axis)
    {
        for (float angle:{-1.0f,0.0f,1.0f})
        {
            const float s=std::sin(angle/2),c=std::cos(angle/2);
            const Quat q{axis==0?s:0,axis==1?s:0,axis==2?s:0,c};
            tracking.headOrientation=q;
            for (Eye& eye:tracking.eyes) eye.orientation=q;
            tracking.eyes[0].offset=Rotate(q,{-0.032f,0,0});
            tracking.eyes[1].offset=Rotate(q,{0.032f,0,0});
            tracking.headPosition={0.3f,-0.2f,0.4f};
            reference.position={-0.1f,0.2f,0.1f};
            check(StageNativeViewPair(native,tracking,reference,0.33f,true,rebuild,staged)==
                PairStageResult::Staged,"rotated and translated tracked pair stages");
            const float* l=staged.cameras[0].pose.matrix;
            const float* r=staged.cameras[1].pose.matrix;
            const Vec3 d{l[12]-r[12],l[13]-r[13],l[14]-r[14]};
            check(near(std::sqrt(Dot(d,d)),0.064f*0.33f*kSaberUnitsPerNativeUnit),
                "IPD preserved through all three physical rotation axes");
        }
    }
    SurfaceTransfer copy{};
    copy.sourceSurface=0x10000; copy.destinationSurface=0x20000;
    copy.width=1920; copy.height=1080;
    check(IsPrimaryEyeTransfer(copy,0,1920,1080),"native first-eye copy admitted");
    copy.destinationY=1080;
    check(IsPrimaryEyeTransfer(copy,1,1920,1080),"native second-eye row admitted");
    check(!IsPrimaryEyeTransfer(copy,0,1920,1080),"eye index cannot claim opposite output row");
    check(!IsPrimaryEyeTransfer(copy,1,1920,1079),"different raster cannot claim the eye copy");
    copy.sourceMip=1;
    check(!IsPrimaryEyeTransfer(copy,1,1920,1080),"mip transfer cannot claim a full-resolution eye");
    copy.sourceMip=0; copy.sourceArraySlice=1;
    check(!IsPrimaryEyeTransfer(copy,1,1920,1080),"unproven array slice rejected");
    copy.sourceArraySlice=0; copy.sourceSurface=copy.destinationSurface;
    check(!IsPrimaryEyeTransfer(copy,1,1920,1080),"aliased source/destination rejected");
    check(!IsPrimaryEyeTransfer(copy,2,1920,1080)&&
        !IsPrimaryEyeTransfer(copy,-1,1920,1080)&&
        !IsPrimaryEyeTransfer(copy,1,0xffffffffu,0xffffffffu),"indices and dimensions bounded");

    // Exercise actual source-list attribution. Stationary cameras can be
    // byte-identical across preparations; pose matching alone is insufficient.
    native=savedNative;
    check(StageNativeViewPair(native,tracking,reference,0.33f,true,rebuild,staged)==
        PairStageResult::Staged,"receipt test pair staged");
    SaberViewPair committed=native;
    for (int eye=0;eye<2;++eye) committed.views[eye].camera=staged.cameras[eye];
    PreparedHandoff handoff;
    const auto direct=handoff.Begin(PreparationOrigin::ActiveList,0x10000,tracking.generation);
    check(handoff.Publish(direct,tracking,staged,committed),"direct preparation receipt published");
    PreparedReceipt received{};
    check(handoff.Read(PreparationOrigin::ActiveList,0x10000,committed,
        tracking.generation,tracking.spaceEpoch,received)&&received.tracking.serial==tracking.serial,
        "active render receives exact tracking pair");
    const auto savedReceipt=received;
    SaberViewPair withAuxiliaryViews=committed;
    withAuxiliaryViews.count=3;
    check(handoff.Read(PreparationOrigin::ActiveList,0x10000,withAuxiliaryViews,
        tracking.generation,tracking.spaceEpoch,received),
        "native culling may append auxiliary views without changing the prepared primary eyes");
    withAuxiliaryViews.count=kNativeViewCapacity;
    check(MatchesPreparedViews(withAuxiliaryViews,received),"native view storage capacity is bounded");
    withAuxiliaryViews.count=kNativeViewCapacity+1;
    check(!MatchesPreparedViews(withAuxiliaryViews,received),"corrupt total view count rejected");
    withAuxiliaryViews.count=3;
    withAuxiliaryViews.views[1].viewIndex=2;
    check(!MatchesPreparedViews(withAuxiliaryViews,received),"auxiliary view cannot replace the right primary eye");
    withAuxiliaryViews=committed; withAuxiliaryViews.count=3;
    check(!handoff.Publish(direct,tracking,staged,withAuxiliaryViews),
        "initial receipt still requires exactly two primary views before native culling");
    check(!handoff.Read(PreparationOrigin::CopiedList,0x10000,committed,
        tracking.generation,tracking.spaceEpoch,received),"other preparation route cannot steal identity");
    check(!handoff.Read(PreparationOrigin::ActiveList,0x20000,committed,
        tracking.generation,tracking.spaceEpoch,received),"similar camera at different source rejected");
    check(!handoff.Read(PreparationOrigin::ActiveList,0x10000,committed,
        tracking.generation+1,tracking.spaceEpoch,received),"retired module generation rejected");
    check(!handoff.Read(PreparationOrigin::ActiveList,0x10000,committed,
        tracking.generation,tracking.spaceEpoch+1,received),"recenter invalidates old tracking space");
    committed.views[1].camera.pose.matrix[12]+=0.01f;
    check(!handoff.Read(PreparationOrigin::ActiveList,0x10000,committed,
        tracking.generation,tracking.spaceEpoch,received),"mixed eye pair rejected");
    check(std::memcmp(&received,&savedReceipt,sizeof(received))==0,
        "failed read never publishes partial or replaced metadata");
    committed.views[1].camera=staged.cameras[1];
    const auto copied=handoff.Begin(PreparationOrigin::CopiedList,0x30000,tracking.generation);
    check(handoff.Publish(copied,tracking,staged,committed),"copied preparation recorded separately");
    SaberViewPair transferred=committed;
    transferred.views[0].camera.derived160[0x200-0x160]=0x7a;
    check(handoff.Read(PreparationOrigin::CopiedList,0x30000,transferred,
        tracking.generation,tracking.spaceEpoch,received)&&received.ticket.revision==copied.revision,
        "native list copy retains source identity while worker-owned resource tail changes");
    const PreparedReceipt frozen=received;
    handoff.Invalidate(PreparationOrigin::CopiedList);
    check(!handoff.Read(PreparationOrigin::CopiedList,0x30000,transferred,
        tracking.generation,tracking.spaceEpoch,received),
        "new stock or failed preparation revokes identical previous cameras");
    check(!handoff.Publish(copied,tracking,staged,committed),
        "late completion cannot restore a revoked preparation");
    check(frozen.tracking.serial==tracking.serial&&frozen.ticket.sourceList==0x30000,
        "already frozen render receipt does not follow a newer preparation");
    check(handoff.Read(PreparationOrigin::ActiveList,0x10000,committed,
        tracking.generation,tracking.spaceEpoch,received),"copied-list invalidation leaves direct slot intact");
    const auto newer=handoff.Begin(PreparationOrigin::ActiveList,0x10000,tracking.generation);
    check(!handoff.Publish(direct,tracking,staged,committed)&&handoff.Current(newer),
        "overlapping old producer cannot publish through reused native list");
    auto mismatched=staged;
    ++mismatched.serial;
    check(!handoff.Publish(newer,tracking,mismatched,committed),"cross-serial receipt rejected");
    check(handoff.Publish(newer,tracking,staged,committed),"failed frame recovers on next valid publication");
    check(!handoff.Read(static_cast<PreparationOrigin>(7),0x10000,committed,
        tracking.generation,tracking.spaceEpoch,received),"unrecognized native route bounded");
    std::atomic<bool> finished=false;
    std::atomic<unsigned> incoherent=0;
    std::thread producer([&] {
        Tracking nextTracking=tracking;
        StagedViewPair nextPair=staged;
        for (uint64_t serial=1000;serial<11000;++serial)
        {
            nextTracking.serial=nextPair.serial=serial;
            nextTracking.predictedDisplayTimeNs=static_cast<int64_t>(serial*10000000);
            nextTracking.controllers.primaryAim={true,{float(serial),1,2},{}};
            nextTracking.controllers.support={true,{-float(serial),3,4},{}};
            nextTracking.controllers.leftHanded=(serial&1)!=0;
            const auto ticket=handoff.Begin(PreparationOrigin::CopiedList,0x40000,tracking.generation);
            (void)handoff.Publish(ticket,nextTracking,nextPair,committed);
        }
        finished.store(true,std::memory_order_release);
    });
    unsigned reads=0;
    while (!finished.load(std::memory_order_acquire))
    {
        PreparedReceipt sample{};
        if (handoff.Read(PreparationOrigin::CopiedList,0x40000,committed,
                tracking.generation,tracking.spaceEpoch,sample))
        {
            ++reads;
            if (sample.tracking.serial!=sample.pair.serial||sample.tracking.serial<1000||
                sample.ticket.sourceList!=0x40000||sample.ticket.generation!=tracking.generation||
                sample.tracking.controllers.primaryAim.position.x!=float(sample.tracking.serial)||
                sample.tracking.controllers.support.position.x!=-float(sample.tracking.serial)||
                sample.tracking.controllers.leftHanded!=((sample.tracking.serial&1)!=0)||
                sample.tracking.predictedDisplayTimeNs!=static_cast<int64_t>(sample.tracking.serial*10000000))
                ++incoherent;
        }
        std::this_thread::yield();
    }
    producer.join();
    // Busy publications may be dropped; the final publication or a fresh one
    // after reader retirement must be available. Do not require a scheduling
    // dependent number of successful racing reads.
    const auto finalTicket=handoff.Begin(PreparationOrigin::CopiedList,0x40000,tracking.generation);
    check(handoff.Publish(finalTicket,tracking,staged,committed)&&
        handoff.Read(PreparationOrigin::CopiedList,0x40000,committed,
            tracking.generation,tracking.spaceEpoch,received)&&incoherent.load()==0,
        "concurrent preparation/publication never mixes tracking payloads and recovers after contention");
    // Several readers may hit the brief writer-owned publication window.
    // Rejected reader pins must retire without clearing the writer flag or
    // underflowing either slot when its writer concurrently finishes.
    struct SnapshotProbe { uint64_t serial{},lanes[32]{}; };
    Snapshot<SnapshotProbe> shared;
    SnapshotProbe probe{};
    check(shared.Publish(probe),"contention fixture publishes its initial snapshot");
    std::atomic<bool> startReaders{},stopReaders{};
    std::atomic<unsigned> tornReads{};
    std::array<std::thread,8> readers;
    for (auto& reader:readers) reader=std::thread([&] {
        while (!startReaders.load(std::memory_order_acquire)) std::this_thread::yield();
        while (!stopReaders.load(std::memory_order_acquire))
        {
            SnapshotProbe value{};
            if (!shared.Read(value)) continue;
            for (uint64_t lane:value.lanes)
                if (lane!=value.serial) tornReads.fetch_add(1,std::memory_order_relaxed);
        }
    });
    startReaders.store(true,std::memory_order_release);
    for (uint64_t serial=1;serial<=50000;++serial)
    {
        probe.serial=serial;for (auto& lane:probe.lanes) lane=serial;
        (void)shared.Publish(probe);
    }
    stopReaders.store(true,std::memory_order_release);
    for (auto& reader:readers) reader.join();
    SnapshotProbe finalProbe{};
    check(tornReads.load()==0&&shared.Publish(probe)&&shared.Publish(probe)&&shared.Read(finalProbe)&&
        finalProbe.serial==probe.serial,
        "writer/rejected-reader overlap preserves coherent payloads and leaves both slots reusable");
    return failures?1:0;
}
