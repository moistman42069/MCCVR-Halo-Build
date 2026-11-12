// The VR action bridge's reader thunks (src/dll/native_vr_reader_thunks.asm) must preserve every volatile
// register but rax across the C++ hook: the hooked native readers are leaves whose LTCG callers keep
// volatile registers live across the call, and a clobbered r10/r11 crashes Halo 3 at every level load.
// An asm probe plays that caller; the hook is the worst case, clobbering everything.
#include <cstdio>
#include <cstdint>

extern "C" {
void NativeVrReaderThunkReach();
void NativeVrReaderThunkHalo3();
void NativeVrH3ActionEdgesThunk();
void NativeVrReaderThunkOdst();
void NativeVrReaderThunkHalo4();
void* (*g_nativeVrReaderReach)(void*,unsigned,uintptr_t)=nullptr;
void* (*g_nativeVrH3ActionEdges)(void*,unsigned,uintptr_t)=nullptr;
void* (*g_nativeVrReaderHalo3)(void*,unsigned,uintptr_t)=nullptr;
void* (*g_nativeVrReaderOdst)(void*,unsigned,uintptr_t)=nullptr;
void* (*g_nativeVrReaderHalo4)(void*,unsigned,uintptr_t)=nullptr;
unsigned ReaderThunkProbe(void (*thunk)(),void* state,unsigned action);
void* ClobberingReader(void*,unsigned,uintptr_t);
extern uint64_t g_probeReturn,g_clobberState,g_clobberAction,g_clobberCaller,g_expectedCaller;
}

static int failures=0;
static void Check(bool ok,const char* what)
{
    std::printf("%s %s\n",ok?"PASS":"FAIL",what);
    if(!ok) ++failures;
}

int main()
{
    struct { const char* name; void (*thunk)(); void* (**hook)(void*,unsigned,uintptr_t); } titles[]{
        {"Reach",&NativeVrReaderThunkReach,&g_nativeVrReaderReach},
        {"Halo 3",&NativeVrReaderThunkHalo3,&g_nativeVrReaderHalo3},
        {"Halo 3 action edges",&NativeVrH3ActionEdgesThunk,&g_nativeVrH3ActionEdges},
        {"ODST",&NativeVrReaderThunkOdst,&g_nativeVrReaderOdst},
        {"Halo 4",&NativeVrReaderThunkHalo4,&g_nativeVrReaderHalo4},
    };
    char state[16]{};
    // Control: without a thunk the clobbering hook loses every probed register, so the probe can fail.
    const unsigned bare=ReaderThunkProbe(reinterpret_cast<void (*)()>(&ClobberingReader),state,0x26);
    Check(bare==0x3F3F,"control: the probe sees a bare C-style hook clobber rcx, rdx, r8-r11 and xmm0-xmm5");
    for(auto& t:titles)
    {
        *t.hook=&ClobberingReader;
        g_probeReturn=g_clobberState=g_clobberAction=0;
        const unsigned lost=ReaderThunkProbe(t.thunk,state,0x26);
        char line[160];
        std::snprintf(line,sizeof(line),"%s thunk preserves rcx, rdx, r8-r11 and xmm0-xmm5 across a clobbering hook (lost mask 0x%X)",t.name,lost);
        Check(lost==0,line);
        std::snprintf(line,sizeof(line),"%s thunk passes the native arguments through and returns the hook's result",t.name);
        Check(g_clobberState==reinterpret_cast<uint64_t>(state) && g_clobberAction==0x26 && g_probeReturn==0x1234,line);
        Check(g_clobberCaller==g_expectedCaller,"thunk forwards the original native caller, not its own return address");
        *t.hook=nullptr;
    }
    std::printf(failures?"%d FAILED\n":"all passed\n",failures);
    return failures?1:0;
}
