#include <windows.h>
#include <intrin.h>
#include <MinHook.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include "native_subtitles.h"
#include "sigscan.h"
#include "title_adapter.h"
#include "vr.h"
#include "../common/config.h"
#include "../common/log.h"
#include "../common/subtitle_logic.h"

namespace {
// Martysl1's two independent MCC UnifiedSubtitles callers select the same
// final UTF-16 handoff. Only the observed production caller is caption ingress.
constexpr char kTimedCaller[] =
    "F3 0F 10 46 34 F3 0F 5C 46 30 8B 43 34 4C 8D 4D FF 48 83 7D 17 08 "
    "4C 0F 43 4D FF 4C 8D 43 2C 48 8D 53 1C 48 8D 4B 0C F3 0F 11 44 24 28 "
    "89 44 24 20 E8 ?? ?? ?? ??";
constexpr char kProductionCaller[] =
    "F3 0F 10 45 7F 4D 8B C4 F3 0F 11 44 24 28 49 8B D7 49 8B CE "
    "44 89 6C 24 20 E8 ?? ?? ?? ?? 48 8B 4D EF 48 33 CC E8 ?? ?? ?? ??";
constexpr unsigned char kTargetPrologue[] = {
    0x48,0x8B,0xC4,0x48,0x89,0x58,0x08,0x48,0x89,0x70,0x10,
    0x48,0x89,0x78,0x18,0x4C,0x89,0x70,0x20,0x55,0x48,0x8B,0xEC,
    0x48,0x81,0xEC,0x80,0x00,0x00,0x00 };
using Handoff = void(__fastcall*)(const void*, const void*, const void*,
    const wchar_t*, uint32_t, float);
Handoff g_original = nullptr;
uintptr_t g_productionReturn = 0;
bool g_attempted = false;
using InterfaceHandoff = bool(__fastcall*)(void*,const void*,const void*,const void*,
    const wchar_t*,float,uint32_t);
InterfaceHandoff g_originalInterface = nullptr;
#include "native_subtitle_authority.inl"
constexpr char kInterfaceWrapper[] =
    "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 40 B1 79 49 8B D9 49 8B F8 "
    "48 8B F2 E8 ?? ?? ?? ?? 84 C0 74 ?? F3 0F 10 44 24 78 4C 8B CB "
    "8B 84 24 80 00 00 00 4C 8B C7 48 8B 0D ?? ?? ?? ?? 48 8B D6 "
    "F3 0F 11 44 24 30 89 44 24 28 48 8B 44 24 70 48 89 44 24 20 E8 ?? ?? ?? ??";

#include "native_subtitle_queue.inl"
struct Slot { std::atomic<unsigned> state{0}; Event event{}; };
Slot g_slots[16];
std::atomic<uint64_t> g_sequence{0}, g_dropped{0};
std::shared_ptr<const NativeSubtitleImage> g_image;
uint64_t g_consumed = 0, g_revision = 0;
RasterCaption g_lastRasterized{};

bool CopyText(const wchar_t* source, Event& event) noexcept {
    if (!source || (reinterpret_cast<uintptr_t>(source) & 1)) return false;
    __try {
        for (uint32_t i = 0; i < subtitles::kTextCapacity; ++i) {
            const wchar_t c = source[i];
            event.text[i] = c;
            if (!c) { event.length = i; return i != 0; }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { }
    return false;
}

uintptr_t Unique(uintptr_t base, size_t size, const char* pattern) {
    const uintptr_t first = sig::Find(base, size, pattern);
    if (!first || first < base || first >= base + size) return 0;
    return sig::Find(first + 1, base + size - first - 1, pattern) ? 0 : first;
}

#include "native_subtitles_sources.inl"

void __fastcall HookHandoff(const void* a, const void* b, const void* c,
    const wchar_t* text, uint32_t style, float seconds) {
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    const GameTitle title = TitleAdapter_GetActiveTitle();
    const RuntimeMode mode = TitleAdapter_GetRuntimeMode();
    // Theatre has a distinct native lifetime source; do not misclassify an
    // ordinary speech handoff as an authored cinematic subtitle.
    if (caller == g_productionReturn &&
        (title == GameTitle::Halo3 || title == GameTitle::Halo3ODST) &&
        (mode == RuntimeMode::Gameplay || mode == RuntimeMode::Vehicle || mode == RuntimeMode::Turret) &&
        !VR_IsCutsceneTheaterActive())
        NativeSubtitles_Capture(title, TitleAdapter_GetGeneration(title), text, seconds, false);
    g_original(a, b, c, text, style, seconds);
}

#include "native_subtitle_interface.inl"

bool __fastcall HookInterfaceHandoff(void* self,const void* a,const void* b,const void* c,
    const wchar_t* text,float seconds,uint32_t style) {
    return DispatchNativeSubtitleInterface(reinterpret_cast<uintptr_t>(_ReturnAddress()),
        self,a,b,c,text,seconds,style);
}

void InstallHostObserver() {
    if (g_attempted) return;
    g_attempted = true;
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    size_t size = 0;
    __try {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) return;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return;
        size = nt->OptionalHeader.SizeOfImage;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    const uintptr_t timed = Unique(base, size, kTimedCaller);
    const uintptr_t production = Unique(base, size, kProductionCaller);
    const uintptr_t target = timed ? sig::RipTarget(timed + 0x32, timed + 0x36) : 0;
    const uintptr_t other = production ? sig::RipTarget(production + 0x1A, production + 0x1E) : 0;
    DWORD64 imageBase = 0;
    const auto* function = target ? RtlLookupFunctionEntry(target, &imageBase, nullptr) : nullptr;
    if (!timed || !production || !target || target != other || target < base ||
        target > base + size - sizeof(kTargetPrologue) || !function ||
        imageBase + function->BeginAddress != target ||
        memcmp(reinterpret_cast<void*>(target), kTargetPrologue, sizeof(kTargetPrologue))) {
        LOG("Native subtitles: shared host handoff proof unavailable; stock subtitles retained");
        return;
    }
    g_productionReturn = production + 0x1E;
    // Superseded by exact title callers at the normal native interface.
    // Keep the earlier observer dormant for the documented rollback path.
    if constexpr(false) {
    const MH_STATUS created = MH_CreateHook(reinterpret_cast<void*>(target),
        reinterpret_cast<void*>(&HookHandoff), reinterpret_cast<void**>(&g_original));
    if (created != MH_OK) {
        LOG("Native subtitles: shared host observer create failed (%d); stock retained", created);
        return;
    }
    const MH_STATUS enabled = MH_EnableHook(reinterpret_cast<void*>(target));
    if (enabled != MH_OK) {
        MH_RemoveHook(reinterpret_cast<void*>(target));
        g_original = nullptr;
        LOG("Native subtitles: shared host observer enable failed (%d); stock retained", enabled);
        return;
    }
    // Host EXE and this injected DLL live for the process; the hook is never
    // removed on title switches. Its ingress checks current title/generation.
    LOG("Native subtitles: H3/ODST shared localized-text observer ready (host +0x%llX); native call preserved",
        static_cast<unsigned long long>(target - base));
    }
    const uintptr_t wrapper=Unique(base,size,kInterfaceWrapper);
    DWORD64 productionImageBase=0,wrapperImageBase=0;
    const auto productionFunction=RtlLookupFunctionEntry(production,&productionImageBase,nullptr);
    const auto wrapperFunction=wrapper?RtlLookupFunctionEntry(wrapper,&wrapperImageBase,nullptr):nullptr;
    const uintptr_t wrapperCallee=wrapper?sig::RipTarget(wrapper+0x55,wrapper+0x59):0;
    if(!wrapper || !productionFunction || !wrapperFunction ||
        wrapperImageBase+wrapperFunction->BeginAddress!=wrapper ||
        productionImageBase+productionFunction->BeginAddress!=wrapperCallee) {
        LOG("Native subtitles: host interface proof unavailable; stock captions retained");
        return;
    }
    const auto wrapperCreated=MH_CreateHook(reinterpret_cast<void*>(wrapper),
        reinterpret_cast<void*>(&HookInterfaceHandoff),reinterpret_cast<void**>(&g_originalInterface));
    if(wrapperCreated!=MH_OK) {
        LOG("Native subtitles: native interface observer create failed (%d); stock retained",wrapperCreated);return;
    }
    const auto wrapperEnabled=MH_EnableHook(reinterpret_cast<void*>(wrapper));
    if(wrapperEnabled!=MH_OK) {
        MH_RemoveHook(reinterpret_cast<void*>(wrapper));g_originalInterface=nullptr;
        LOG("Native subtitles: native interface observer enable failed (%d); stock retained",wrapperEnabled);return;
    }
    LOG("Native subtitles: native interface observer ready (host +0x%llX); return/caller proof required",
        static_cast<unsigned long long>(wrapper-base));
}

std::shared_ptr<const NativeSubtitleImage> Rasterize(const RasterCaption& event) {
    const wchar_t* normalized=event.text;
    if(!event.length) return {};
    constexpr int w = NativeSubtitleImage::width, h = NativeSubtitleImage::height;
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) return {};
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = w; info.bmiHeader.biHeight = -h;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap || !bits) { if (bitmap) DeleteObject(bitmap); DeleteDC(dc); return {}; }
    const auto previousBitmap = SelectObject(dc, bitmap);
    memset(bits, 0, w * h * 4);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(255,255,255));
    HFONT font = nullptr;
    HGDIOBJ previousFont = nullptr;
    RECT measured{48,24,w-48,h-24};
    for (int px = 66; px >= 18; px -= 4) {
        font = CreateFontW(-px, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        if (!font) break;
        previousFont = SelectObject(dc, font);
        measured = {48,24,w-48,h-24};
        DrawTextW(dc, normalized, -1, &measured, DT_CENTER|DT_WORDBREAK|DT_NOPREFIX|DT_CALCRECT);
        if (measured.bottom - measured.top <= h - 48 || px == 18) break;
        SelectObject(dc, previousFont); DeleteObject(font); font = nullptr;
    }
    if (!font) {
        SelectObject(dc,previousBitmap);DeleteObject(bitmap);DeleteDC(dc);return {};
    }
    if (font) {
        const int textHeight = measured.bottom - measured.top;
        RECT target{48,(h-textHeight)/2,w-48,(h+textHeight)/2};
        DrawTextW(dc, normalized, -1, &target, DT_CENTER|DT_WORDBREAK|DT_NOPREFIX);
        GdiFlush();
    }
    auto result = std::make_shared<NativeSubtitleImage>();
    result->title = event.title; result->generation = event.generation;
    result->expiresAtMs = event.expires; result->revision = ++g_revision;
    result->theatre = event.theatre;
    auto rgba = std::make_shared<std::vector<uint8_t>>(w*h*4);
    // A bounded two-pass dilation supplies a black outline. RGB contains
    // premultiplied white coverage; alpha additionally contains the outline.
    std::vector<uint8_t> dilation(w*h);
    const auto* pixels = static_cast<const uint8_t*>(bits);
    for (int y=0; y<h; ++y) for (int x=0; x<w; ++x) {
        uint8_t a = 0;
        for (int dx=-3; dx<=3; ++dx)
            if (x+dx>=0 && x+dx<w) a=std::max(a,pixels[4*(y*w+x+dx)]);
        dilation[y*w+x] = a;
    }
    for (int y=0; y<h; ++y) for (int x=0; x<w; ++x) {
        const int i=y*w+x;
        uint8_t a=0;
        for (int dy=-3; dy<=3; ++dy)
            if (y+dy>=0 && y+dy<h) a=std::max(a,dilation[(y+dy)*w+x]);
        const uint8_t white=pixels[4*i];
        (*rgba)[4*i]=(*rgba)[4*i+1]=(*rgba)[4*i+2]=white;
        (*rgba)[4*i+3]=a;
    }
    if (font) { SelectObject(dc,previousFont); DeleteObject(font); }
    SelectObject(dc,previousBitmap); DeleteObject(bitmap); DeleteDC(dc);
    result->rgba = rgba;
    return result;
}
}

void NativeSubtitles_Capture(GameTitle title, uint32_t generation,
    const wchar_t* text, float seconds, bool theatre, uint8_t channel, bool refreshed) noexcept {
    const uint32_t duration = subtitles::DurationMs(seconds);
    if (!generation || !duration || title == GameTitle::None || channel>2) return;
    for (auto& slot : g_slots) {
        unsigned empty=0;
        if (!slot.state.compare_exchange_strong(empty,1,std::memory_order_acquire)) continue;
        auto& e=slot.event;
        e.title=title; e.generation=generation; e.theatre=theatre; e.channel=channel;
        // Renderer-fed captions renew every frame. Revoked native queues expire
        // promptly even when a cinematic is skipped without changing title.
        e.expires=GetTickCount64()+(refreshed?std::min(duration,250u):duration);
        e.sequence=g_sequence.fetch_add(1,std::memory_order_relaxed)+1;
        if (CopyText(text,e)) slot.state.store(2,std::memory_order_release);
        else slot.state.store(0,std::memory_order_release);
        return;
    }
    g_dropped.fetch_add(1,std::memory_order_relaxed);
}

std::shared_ptr<const NativeSubtitleImage> NativeSubtitles_Read() {
    return std::atomic_load_explicit(&g_image,std::memory_order_acquire);
}

void NativeSubtitles_SetReachSources(uint32_t generation,
    uintptr_t gameplayReturn,uintptr_t theatreReturn) noexcept {
    const SubtitleSource sources[]={{gameplayReturn,0,false},{theatreReturn,0,true}};
    SetSubtitleSources(GameTitle::HaloReach,generation,sources,_countof(sources));
}

void NativeSubtitles_Poll(bool levelRunning) {
    InstallHostObserver();
    PollNativeSubtitleSources(levelRunning);
    const auto title=TitleAdapter_GetActiveTitle();
    const auto generation=TitleAdapter_GetGeneration(title);
    const auto now=GetTickCount64();
    const bool theatre=VR_IsCutsceneTheaterActive();
    Event pending[_countof(g_slots)]{};size_t count=0;
    for(auto& slot:g_slots) {
        unsigned ready=2;
        if(!slot.state.compare_exchange_strong(ready,3,std::memory_order_acquire)) continue;
        if(slot.event.sequence>g_consumed) pending[count++]=slot.event;
        slot.state.store(0,std::memory_order_release);
    }
    std::sort(pending,pending+count,[](const Event& a,const Event& b){return a.sequence<b.sequence;});
    for(size_t i=0;i<count;++i) {
        g_consumed=std::max(g_consumed,pending[i].sequence);
        if(subtitles::Current(pending[i].title,pending[i].generation,pending[i].expires,title,generation,now)&&
            pending[i].theatre==theatre) AcceptSubtitleEvent(pending[i]);
    }
    RasterCaption caption{};
    auto current=NativeSubtitles_Read();
    if(!levelRunning||!ComposeSubtitleCaption(title,generation,theatre,now,caption)) {
        if(!levelRunning) {g_captionLines[0]={};g_captionLines[1]={};g_captionPair=false;}
        std::atomic_store_explicit(&g_image,std::shared_ptr<const NativeSubtitleImage>{},std::memory_order_release);
    } else {
        std::shared_ptr<const NativeSubtitleImage> image;
        if(current&&current->title==title&&current->generation==generation&&current->theatre==theatre&&
            g_lastRasterized.length==caption.length&&
            !memcmp(g_lastRasterized.text,caption.text,caption.length*sizeof(wchar_t))) {
            auto refreshed=std::make_shared<NativeSubtitleImage>(*current);
            refreshed->expiresAtMs=caption.expires;image=refreshed;
        } else {
            image=Rasterize(caption);
            if(image) g_lastRasterized=caption;
        }
        if(image) std::atomic_store_explicit(&g_image,image,std::memory_order_release);
    }
    const auto dropped=g_dropped.exchange(0,std::memory_order_relaxed);
    if (dropped) LOG("Native subtitles: bounded queue dropped %llu captions; native UI unchanged",
        static_cast<unsigned long long>(dropped));
}
