// Roomscale's admission (src/common/roomscale_admission_logic.h): on foot, in control, first person -- by the
// game's own camera where a title has it, else by the first-person view model -- and the reading that refused.
// Pure logic: no clock, no engine. The camera -> follow wiring is tested in roomscale_input_tests.cpp.
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>
#include "../src/common/roomscale_admission_logic.h"

int RunRoomscaleAdmissionTests()
{
    using namespace roomscale_admission;
    int failures=0;
    auto Check=[&](bool ok,const char* message) {
        if (!ok) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
    };

    // The evidence each title supplies for a player standing on foot, in control, with a weapon in his hands.
    // Halo 3: the director's camera, the game's Monitor predicate, the first-person interpolation witness.
    const auto halo3=[]{
        TitleEvidence e{};
        e.cinematic=Signal::No; e.unit=Signal::Yes; e.seated=Signal::No;
        e.camera=Signal::Yes; e.monitor=Signal::No; e.viewModel=Signal::Yes;
        return e;
    };
    // ODST and CE: a camera rung, and no Forge (the Monitor is No by construction). CE has no view-model witness.
    const auto odst=[&]{ return halo3(); };
    const auto ce=[&]{ TitleEvidence e=halo3(); e.controlBlocked=Signal::No; e.viewModel=Signal::Unavailable; return e; };
    // Reach, Halo 4, Halo 2 where their camera rung did not bind or was condemned (director_camera_logic.h; with
    // it they read like Halo 3 / ODST, tests/director_camera_tests.cpp): the view model is the first-person proof.
    const auto witnessOnly=[]{
        TitleEvidence e{};
        e.cinematic=Signal::No; e.unit=Signal::Yes; e.seated=Signal::No; e.viewModel=Signal::Yes;
        return e;
    };

    // ---- Armed, on foot, first person: every title admits ------------------------------------------
    Check(ClassifyTitle(halo3())==Refusal::None, "Halo 3, armed first person: admitted");
    Check(ClassifyTitle(odst())==Refusal::None, "ODST, armed first person: admitted");
    Check(ClassifyTitle(ce())==Refusal::None, "CE, first person (it has no view-model witness): admitted");
    Check(ClassifyTitle(witnessOnly())==Refusal::None, "Reach / Halo 4 / Halo 2, view model drawn: admitted");

    // ---- UNARMED first person (Halo 3: the start of Sierra 117) -------------------------------------
    {
        // The director says first-person camera, first-person perspective; no view model is drawn (no weapon);
        // the game says the player is not Forge's Monitor (a campaign has no game engine at all).
        TitleEvidence e=halo3(); e.viewModel=Signal::No;
        Check(ClassifyTitle(e)==Refusal::None,
            "Halo 3 unarmed, the game's camera first person and not the Monitor: admitted without a view model");
        TitleEvidence o=odst(); o.viewModel=Signal::No;
        Check(ClassifyTitle(o)==Refusal::None, "ODST unarmed, the game's camera first person: admitted");
        // A native zoom stops the view model the same way and is first person just the same.
        Check(ClassifyTitle(e)==Refusal::None, "Halo 3 zoomed (no view model, first-person camera): admitted");
    }
    {
        // Without the Monitor answer the camera rung alone must not admit an unarmed unit: fail closed.
        TitleEvidence e=halo3(); e.viewModel=Signal::No; e.monitor=Signal::Unavailable;
        Check(ClassifyTitle(e)==Refusal::MonitorUnknown,
            "Halo 3 unarmed with the Monitor check unbound: refused, as before the change");
        e.monitor=Signal::Unread;
        Check(ClassifyTitle(e)==Refusal::MonitorUnknown, "Halo 3 unarmed with the Monitor check unread: refused");
        // ... and an armed player is not affected by a missing Monitor check.
        e.viewModel=Signal::Yes;
        Check(ClassifyTitle(e)==Refusal::None, "Halo 3 armed with the Monitor check unbound: admitted");
        // A build with neither the Monitor check nor the optional witness hook admits.
        e.viewModel=Signal::Unavailable;
        Check(ClassifyTitle(e)==Refusal::None, "Halo 3 with no witness hook and no Monitor check: admitted as before");
    }
    {
        // A title with no camera rung on this build cannot tell an unarmed player from a third-person camera.
        TitleEvidence e=witnessOnly(); e.viewModel=Signal::No;
        Check(ClassifyTitle(e)==Refusal::NoFirstPersonModel,
            "Reach / Halo 4 / Halo 2 with no view model: refused, and the reason says which reading");
        e.viewModel=Signal::Unavailable;
        Check(ClassifyTitle(e)==Refusal::None, "Halo 4 without its optional witness hook: admitted as before");
    }

    // ---- Forge's Monitor: unarmed, on foot, player-controlled, and the director's camera is first person ----
    {
        TitleEvidence e=halo3(); e.viewModel=Signal::No; e.monitor=Signal::Yes;
        Check(ClassifyTitle(e)==Refusal::Monitor, "the Monitor (first-person camera, no view model): refused");
        e.viewModel=Signal::Yes;
        Check(ClassifyTitle(e)==Refusal::Monitor, "the Monitor is refused on the game's word, whatever is drawn");
        // Holding an object the director switches to its mode-8 camera: not the first-person camera either.
        e.camera=Signal::No; e.viewModel=Signal::No;
        Check(ClassifyTitle(e)==Refusal::Monitor, "the Monitor holding an object: refused");
        e.monitor=Signal::Unavailable;
        Check(ClassifyTitle(e)==Refusal::ThirdPersonCamera,
            "the Monitor's held-object camera with the check unbound: still refused, by the camera");
        // Back to a Spartan in the same Forge session: the predicate says no again.
        TitleEvidence spartan=halo3();
        Check(ClassifyTitle(spartan)==Refusal::None, "a Forge Spartan (not editing): admitted");
        // On Reach / Halo 4 without their camera rung the Monitor draws no view model, which keeps it out.
        TitleEvidence reach=witnessOnly(); reach.viewModel=Signal::No;
        Check(ClassifyTitle(reach)==Refusal::NoFirstPersonModel, "Reach's Monitor (no view model): refused");
    }

    // ---- The stand-downs that are real stay -----------------------------------------------------------
    {
        TitleEvidence e=halo3(); e.seated=Signal::Yes;
        Check(ClassifyTitle(e)==Refusal::Seated,
            "a seat or a turret: refused, although the mod makes Halo 3's seats first person (the camera says yes)");
        e=halo3(); e.cinematic=Signal::Yes;
        Check(ClassifyTitle(e)==Refusal::Cinematic, "a cinematic: refused");
        e=halo3(); e.cinematic=Signal::Unread;
        Check(ClassifyTitle(e)==Refusal::StaleSample, "a cinematic state that cannot be read is not 'player-controlled'");
        e=halo3(); e.unit=Signal::No; e.camera=Signal::No; e.viewModel=Signal::No;
        Check(ClassifyTitle(e)==Refusal::NoUnit, "death (no unit): refused, named before the camera");
        e=halo3(); e.camera=Signal::No;
        Check(ClassifyTitle(e)==Refusal::ThirdPersonCamera,
            "a third-person / orbit / scripted camera: refused even with a view model inside its window");
        e=halo3(); e.seated=Signal::Unread;
        Check(ClassifyTitle(e)==Refusal::StaleSample, "a stale seat sample is never 'on foot'");
        e=halo3(); e.unit=Signal::Unread;
        Check(ClassifyTitle(e)==Refusal::StaleSample, "an unreadable unit is never 'on foot'");
        e=ce(); e.controlBlocked=Signal::Yes;
        Check(ClassifyTitle(e)==Refusal::ControlBlocked, "CE with the player's input or look taken away: refused");
        e=ce(); e.camera=Signal::No;
        Check(ClassifyTitle(e)==Refusal::ThirdPersonCamera, "CE in a third-person perspective: refused");
    }
    {
        // A camera rung that is bound but did not read this sample: the view model still vouches for an armed
        // player; with nothing to vouch it refuses and says so.
        TitleEvidence e=halo3(); e.camera=Signal::Unread;
        Check(ClassifyTitle(e)==Refusal::None, "director unread for a frame, view model drawn: admitted");
        e.viewModel=Signal::No;
        Check(ClassifyTitle(e)==Refusal::NoFirstPersonModel, "director unread and no view model: refused");
        e.viewModel=Signal::Unavailable;
        Check(ClassifyTitle(e)==Refusal::CameraUnread, "director unread and no witness hook: refused as unread");
    }

    // ---- The director record as the admission reads it ---------------------------------------------------
    Check(fp_view::DirectorCameraSignal(true,true,3,0)==Signal::Yes, "director mode 3, perspective 0: first person");
    Check(fp_view::DirectorCameraSignal(true,true,0,1)==Signal::No, "the following (chase) camera: not first person");
    Check(fp_view::DirectorCameraSignal(true,true,4,1)==Signal::No, "the dead camera: not first person");
    Check(fp_view::DirectorCameraSignal(true,true,8,0)==Signal::No,
        "mode 8 (the Monitor's held-object camera, outside the record's named range): not first person");
    Check(fp_view::DirectorCameraSignal(true,true,3,1)==Signal::No, "the first-person camera from another perspective: no");
    Check(fp_view::DirectorCameraSignal(true,false,-1,-1)==Signal::Unread, "bound, not read: unread");
    Check(fp_view::DirectorCameraSignal(false,false,-1,-1)==Signal::Unavailable, "not bound: the next rung decides");

    // ---- The presentation gates, and the reasons the published runtime mode names -------------------------
    {
        Presentation p{};
        p.titleActive=true; p.enabled=true; p.mode=RuntimeMode::Gameplay;
        Check(ClassifyPresentation(p)==Refusal::None, "active title, enabled, gameplay: the title decides");
        Presentation q=p; q.titleActive=false;
        Check(ClassifyPresentation(q)==Refusal::WrongTitle, "another title's callback: refused");
        q=p; q.enabled=false;
        Check(ClassifyPresentation(q)==Refusal::Disabled, "roomscale off / VR not presenting: refused");
        q=p; q.menu=true;
        Check(ClassifyPresentation(q)==Refusal::Menu, "the mod menu or a pause: refused");
        q=p; q.theatre=true;
        Check(ClassifyPresentation(q)==Refusal::Cinematic, "the cutscene theatre: refused as a cinematic");
        q=p; q.mode=RuntimeMode::Loading;
        Check(ClassifyPresentation(q)==Refusal::NotGameplay, "a load: not gameplay");
        q.mode=RuntimeMode::Shell;
        Check(ClassifyPresentation(q)==Refusal::NotGameplay, "the shell: not gameplay");
        q.mode=RuntimeMode::Unsupported;
        Check(ClassifyPresentation(q)==Refusal::NotGameplay, "an unsupported state: not gameplay");
        q.mode=RuntimeMode::Paused;
        Check(ClassifyPresentation(q)==Refusal::Menu, "a published pause: menu / pause");
        q.mode=RuntimeMode::Vehicle;
        Check(ClassifyPresentation(q)==Refusal::Seated, "a published vehicle mode: a seat");
        q.mode=RuntimeMode::Turret;
        Check(ClassifyPresentation(q)==Refusal::Seated, "a published turret mode: a seat");
        q.mode=RuntimeMode::Cutscene;
        Check(ClassifyPresentation(q)==Refusal::Cinematic, "a published cutscene mode: a cinematic");
        q.mode=RuntimeMode::Dead;
        Check(ClassifyPresentation(q)==Refusal::NoUnit, "a published death: no unit");

        // The whole decision, and the rungs it carries for the report.
        TitleEvidence unarmed=halo3(); unarmed.viewModel=Signal::No;
        const Verdict admitted=Decide(p,unarmed);
        Check(admitted.Admitted() && RungCamera(admitted.rungs)==Signal::Yes &&
              RungMonitor(admitted.rungs)==Signal::No && RungViewModel(admitted.rungs)==Signal::No,
            "the verdict carries the three first-person rungs as they stood");
        q=p; q.menu=true;
        Check(Decide(q,halo3()).refusal==Refusal::Menu, "a presentation gate refuses before the title is asked");
        Check(!Verdict{}.Admitted(), "a default verdict is a refusal");
    }

    // ---- A title switch inside one MCC session (ODST -> Halo 3) -------------------------------------------
    {
        // The callback of the title that is no longer active is refused outright...
        Presentation old{}; old.titleActive=false; old.enabled=true; old.mode=RuntimeMode::Gameplay;
        Check(Decide(old,odst()).refusal==Refusal::WrongTitle, "the previous title's late callback: refused");
        // ... the new title reads nothing of the old one's: its own publication is not there yet (another
        // generation's is never fresh), so the camera rung is Unread until its camera thread publishes ...
        Presentation now{}; now.titleActive=true; now.enabled=true; now.mode=RuntimeMode::Loading;
        TitleEvidence loading{};
        loading.seated=Signal::Unread; loading.camera=Signal::Unread; loading.viewModel=Signal::No;
        Check(Decide(now,loading).refusal==Refusal::NotGameplay, "the new title while it loads: not gameplay");
        now.mode=RuntimeMode::Gameplay;
        Check(Decide(now,loading).refusal==Refusal::StaleSample, "the new title before its first sample: stale");
        // ... and once it has, the same rule admits it: nothing has to be cleared by hand.
        TitleEvidence unarmed=halo3(); unarmed.viewModel=Signal::No;
        Check(Decide(now,unarmed).Admitted(), "the new title's first fresh sample re-arms the admission, unarmed or not");
        Check(Decide(now,halo3()).Admitted(), "and armed");
    }

    // ---- Every reason has a name (the dev line prints them) -------------------------------------------------
    {
        bool named=true;
        for (size_t i=0;i<size_t(Refusal::Count);++i)
        {
            const char* name=RefusalName(Refusal(i));
            named=named && name && name[0] && name[0]!='?';
        }
        Check(named, "every refusal has a name");
        Check(std::strcmp(SignalName(Signal::Unavailable),"none")==0 && std::strcmp(SignalName(Signal::Yes),"yes")==0,
            "the rungs print as words");
    }
    {
        // The report's tail for a two-second window of refusals: 121 frames, all for want of a view model.
        uint64_t counts[size_t(Refusal::Count)]{};
        counts[size_t(Refusal::NoFirstPersonModel)]=121;
        char line[512];
        const int used=FormatRefusals(line,sizeof line,counts,Refusal::NoFirstPersonModel,
            PackRungs(Signal::Unread,Signal::Unavailable,Signal::No));
        Check(used>0 && size_t(used)==std::strlen(line) &&
              std::strcmp(line," no first-person view model=121; at the last one (no first-person view model):"
                  " game camera first person=unread, Forge Monitor=none, first-person view model=no")==0,
            "the dev line names the reading that refused, its count and the rungs at the last refusal");
        // Several reasons in one window, in a fixed order; an admitted count is never printed.
        counts[size_t(Refusal::None)]=7; counts[size_t(Refusal::Seated)]=40; counts[size_t(Refusal::Menu)]=3;
        FormatRefusals(line,sizeof line,counts,Refusal::Seated,PackRungs(Signal::Yes,Signal::No,Signal::Yes));
        Check(std::strstr(line," menu or pause=3; seat or turret=40; no first-person view model=121;")==line &&
              !std::strstr(line,"admitted"), "every counted reason is listed once, in order");
        // Every counter at its largest into buffers of every small size: always terminated, never past the end.
        for (auto& count:counts) count=UINT64_MAX;
        bool bounded=true;
        for (size_t size=1;size<=96;++size)
        {
            char small[97]; std::memset(small,0x7F,sizeof small);
            const int wrote=FormatRefusals(small,size,counts,Refusal::Count,0xFF);
            bounded=bounded && wrote>=0 && size_t(wrote)<size && small[wrote]==0 && small[size]==0x7F;
        }
        Check(bounded, "the dev line is truncated, never overrun, whatever the buffer");
        char big[2048];
        const int all=FormatRefusals(big,sizeof big,counts,Refusal::Monitor,0);
        Check(all>0 && size_t(all)<1024-80, "every reason at its largest count fits the report's line");
        Check(FormatRefusals(nullptr,10,counts,Refusal::None,0)==0 && FormatRefusals(big,0,counts,Refusal::None,0)==0,
            "no buffer: nothing written");
    }

    // ---- Halo 3's Monitor predicate: the two sites must prove each other -----------------------------------
    {
        // A synthetic module: the predicate at 0x1000, the unit getter at 0x2000, the player getter at 0x2100,
        // the director's call site at 0x3000, the engine TLS index at 0x9000.
        std::vector<uint8_t> image(0xA000,0xCC);
        const auto put32=[&](size_t at,int32_t value){ std::memcpy(&image[at],&value,4); };
        const int64_t predicateRva=0x1000, unitRva=0x2000, playerRva=0x2100, siteRva=0x3000, tlsRva=0x9000;
        // The predicate's TLS operand.
        put32(size_t(predicateRva)+kSandboxEditorPredicateTlsDisp,
            int32_t(tlsRva-(predicateRva+int64_t(kSandboxEditorPredicateTlsNext))));
        const auto getter=[&](int64_t rva,uint32_t field){
            put32(size_t(rva)+kOutputUserGetterTlsDisp,int32_t(tlsRva-(rva+int64_t(kOutputUserGetterTlsNext))));
            put32(size_t(rva)+kOutputUserGetterBlock,0x110);
            put32(size_t(rva)+kOutputUserGetterField,int32_t(field));
        };
        getter(unitRva,0xC8); getter(playerRva,0xB8);
        const auto call=[&](size_t at,int64_t target){
            image[size_t(siteRva)+at]=0xE8; put32(size_t(siteRva)+at+1,int32_t(target-(siteRva+int64_t(at)+5)));
        };
        call(kDirectorEditorCallPlayer,playerRva); call(kDirectorEditorCallUnit,unitRva);
        call(kDirectorEditorCallPredicate,predicateRva);
        const uint8_t* base=image.data();
        SandboxEditorSites sites{};
        const auto decode=[&](bool playerBody=true){
            sites={};
            return DecodeSandboxEditorSites(base+predicateRva,predicateRva,base+siteRva,siteRva,
                base+playerRva,playerBody,base+unitRva,unitRva,tlsRva,sites);
        };
        Check(decode()==SandboxEditorBind::Bound && sites.predicateRva==predicateRva && sites.playerGetterRva==playerRva,
            "the predicate, the director's call and both getters agree: bound");
        Check(CallTargetRva(base+siteRva,kDirectorEditorCallPredicate,siteRva)==predicateRva &&
              RipTargetRva(base+predicateRva,kSandboxEditorPredicateTlsDisp,kSandboxEditorPredicateTlsNext,predicateRva)==tlsRva,
            "rel32 calls and rip-relative operands decode to module offsets");
        Check(DecodeSandboxEditorSites(nullptr,0,base+siteRva,siteRva,base+playerRva,true,base+unitRva,unitRva,
                tlsRva,sites)==SandboxEditorBind::PredicateMissing, "no unique predicate body: nothing binds");
        Check(DecodeSandboxEditorSites(base+predicateRva,predicateRva,nullptr,0,base+playerRva,true,base+unitRva,
                unitRva,tlsRva,sites)==SandboxEditorBind::CallSiteMissing, "no unique call site: nothing binds");
        Check(decode(false)==SandboxEditorBind::PlayerGetterBody, "a first call target without the getter's body: refused");
        Check(DecodeSandboxEditorSites(base+predicateRva,predicateRva,base+siteRva,siteRva,base+playerRva,true,
                base+unitRva,unitRva,tlsRva+4,sites)==SandboxEditorBind::TlsMismatch,
            "a TLS operand that is not the engine TLS index: refused");
        Check(DecodeSandboxEditorSites(base+predicateRva,predicateRva,base+siteRva,siteRva,base+playerRva,true,
                base+unitRva+0x40,unitRva+0x40,tlsRva,sites)==SandboxEditorBind::UnitGetterMismatch,
            "the call site's unit getter is not the one bound elsewhere: refused");
        call(kDirectorEditorCallPredicate,predicateRva+0x10);
        Check(decode()==SandboxEditorBind::TargetMismatch, "the director calls something other than the matched body: refused");
        call(kDirectorEditorCallPredicate,predicateRva);
        call(kDirectorEditorCallPlayer,unitRva);
        Check(DecodeSandboxEditorSites(base+predicateRva,predicateRva,base+siteRva,siteRva,base+unitRva,true,
                base+unitRva,unitRva,tlsRva,sites)==SandboxEditorBind::PlayerGetterTwin,
            "a player getter that is the unit getter itself: refused");
        call(kDirectorEditorCallPlayer,playerRva);
        getter(playerRva,0xC8);
        Check(decode()==SandboxEditorBind::PlayerGetterTwin, "a player getter reading the unit's field: refused");
        getter(playerRva,0xB8);
        Check(decode()==SandboxEditorBind::Bound, "restored: bound again");
        // The patterns are as long as the offsets into them assume.
        const auto tokens=[](const char* pattern){ size_t n=0; bool in=false;
            for (const char* c=pattern;*c;++c) { if (*c==' ') in=false; else if (!in) { in=true; ++n; } } return n; };
        Check(tokens(kHalo3SandboxEditorPredicatePattern)==kSandboxEditorPredicateBytes &&
              tokens(kHalo3DirectorEditorCallSitePattern)==kDirectorEditorCallSiteBytes &&
              tokens(kHalo3OutputUserPlayerGetterPattern)==kOutputUserPlayerGetterBytes,
            "the three signatures are as long as their decoders assume");
        bool namedBind=true;
        for (size_t i=0;i<size_t(SandboxEditorBind::Count);++i)
        {
            const char* name=SandboxEditorBindName(SandboxEditorBind(i));
            namedBind=namedBind && name && name[0]!='?';
        }
        Check(namedBind, "every bind failure has a name");
    }
    return failures;
}
