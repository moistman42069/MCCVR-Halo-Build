#include "native_fault_probe.h"
#include "../common/log.h"
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <string>

namespace
{
// Records are immutable after ready. Multiple faulting threads claim distinct
// slots without the log mutex, heap allocation or engine calls. One record per
// instruction address bounds output from deliberately guarded native reads.
struct FaultRecord
{
    std::atomic<uintptr_t> address{};
    std::atomic<bool> ready{};
    char text[4096]{};
    bool reported{}; // worker only
};
FaultRecord records[64];
HANDLE file=INVALID_HANDLE_VALUE;
PVOID handler{};
thread_local bool recording{};

struct Text
{
    char* bytes; size_t count{},capacity;
    void Add(const char* s) noexcept
    { while(*s && count+1<capacity) bytes[count++]=*s++;bytes[count]=0; }
    void Hex(uint64_t value) noexcept
    {
        Add("0x");
        for(int shift=60;shift>=0;shift-=4)
        { const char digit[]{"0123456789ABCDEF"[(value>>shift)&15],0};Add(digit); }
    }
    void Address(uintptr_t address) noexcept
    {
        Hex(address);
        MEMORY_BASIC_INFORMATION info{};
        if(address && VirtualQuery(reinterpret_cast<void*>(address),&info,sizeof(info)) &&
            info.Type==MEM_IMAGE)
        {
            Add(" image=");Hex(reinterpret_cast<uintptr_t>(info.AllocationBase));
            Add(" rva=");Hex(address-reinterpret_cast<uintptr_t>(info.AllocationBase));
        }
    }
};

bool Relevant(DWORD code) noexcept
{
    // Do not treat C++ exceptions, debugger breakpoints or guard-page probes
    // as crashes. Stack-overflow handling cannot safely use this stack budget.
    switch(code)
    {
    case EXCEPTION_ACCESS_VIOLATION: case EXCEPTION_IN_PAGE_ERROR:
    case EXCEPTION_ILLEGAL_INSTRUCTION: case EXCEPTION_PRIV_INSTRUCTION:
    case EXCEPTION_INT_DIVIDE_BY_ZERO: case EXCEPTION_FLT_DIVIDE_BY_ZERO:
    case EXCEPTION_FLT_INVALID_OPERATION:return true;
    default:return false;
    }
}

void Capture(EXCEPTION_POINTERS* exception)
{
    const auto& native=*exception->ExceptionRecord;
    const auto& context=*exception->ContextRecord;
    const uintptr_t address=reinterpret_cast<uintptr_t>(native.ExceptionAddress);
    if(!address || !Relevant(native.ExceptionCode))return;
    FaultRecord* record=nullptr;
    for(auto& candidate:records)
    {
        uintptr_t expected=0;
        if(candidate.address.compare_exchange_strong(expected,address,std::memory_order_acq_rel))
        {record=&candidate;break;}
        if(expected==address)return;
    }
    if(!record)return;
    Text out{record->text,0,sizeof(record->text)};
    out.Add("NATIVE FIRST-CHANCE FAULT (may be caught by the game; not proof of a process crash)\r\n");
    out.Add("tick_ms=");out.Hex(GetTickCount64());out.Add(" thread=");out.Hex(GetCurrentThreadId());
    out.Add(" code=");out.Hex(native.ExceptionCode);out.Add(" flags=");out.Hex(native.ExceptionFlags);
    out.Add("\r\nip=");out.Address(address);
    if((native.ExceptionCode==EXCEPTION_ACCESS_VIOLATION || native.ExceptionCode==EXCEPTION_IN_PAGE_ERROR) &&
        native.NumberParameters>=2)
    {
        out.Add(" access(0=read,1=write,8=execute)=");out.Hex(native.ExceptionInformation[0]);
        out.Add(" target=");out.Hex(native.ExceptionInformation[1]);
    }
    out.Add("\r\nrsp=");out.Hex(context.Rsp);out.Add(" rbp=");out.Hex(context.Rbp);
    out.Add(" rcx=");out.Hex(context.Rcx);out.Add(" rdx=");out.Hex(context.Rdx);
    out.Add(" r8=");out.Hex(context.R8);out.Add(" r9=");out.Hex(context.R9);
    // This is the exception-dispatch backtrace, not a claim that every frame
    // was unwound from the saved fault context. IP/registers above are exact.
    out.Add("\r\nexception-dispatch stack:\r\n");
    void* frames[24]{};
    __try
    {
        const USHORT count=CaptureStackBackTrace(0,24,frames,nullptr);
        for(USHORT i=0;i<count;++i)
        {out.Add("  ");out.Address(reinterpret_cast<uintptr_t>(frames[i]));out.Add("\r\n");}
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {out.Add("  unavailable; saved fault address/registers above remain valid\r\n");}
    out.Add("END FAULT\r\n");
    // The separate, preopened write-through file survives a process exit
    // before the worker can mirror the record into HaloMCCVR.log. No normal
    // firing/render hook performs I/O, and the handler never takes Logf's lock.
    DWORD written{};
    WriteFile(file,record->text,static_cast<DWORD>(out.count),&written,nullptr);
    record->ready.store(true,std::memory_order_release);
}

LONG CALLBACK Observe(EXCEPTION_POINTERS* exception)
{
    if(recording || !exception || !exception->ExceptionRecord || !exception->ContextRecord)
        return EXCEPTION_CONTINUE_SEARCH;
    const DWORD lastError=GetLastError();
    recording=true;
    __try { Capture(exception); }
    __except(EXCEPTION_EXECUTE_HANDLER) { /* Only a diagnostic failure. */ }
    recording=false;
    SetLastError(lastError);
    return EXCEPTION_CONTINUE_SEARCH;
}
}

bool NativeFaultProbe_Init(const wchar_t* directory,const char* source)
{
    if(handler)return true;
    const std::wstring path=std::wstring(directory)+L"HaloMCCVR-native-faults.log";
    MoveFileExW(path.c_str(),(path+L".prev").c_str(),MOVEFILE_REPLACE_EXISTING);
    file=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    char header[512]{};Text out{header,0,sizeof(header)};
    out.Add("HaloMCCVR native fault evidence; source=");out.Add(source);
    out.Add(" pid=");out.Hex(GetCurrentProcessId());out.Add("\r\n");
    DWORD written{};WriteFile(file,header,static_cast<DWORD>(out.count),&written,nullptr);
    handler=AddVectoredExceptionHandler(1,Observe);
    if(!handler){CloseHandle(file);file=INVALID_HANDLE_VALUE;return false;}
    return true;
}

void NativeFaultProbe_Poll()
{
    if(!handler)return;
    static uint64_t last{};
    const uint64_t now=GetTickCount64();
    if(last && now-last<1000)return;
    last=now;
    // Module names/addresses are resolved on the worker, never by the handler.
    static constexpr const wchar_t* names[]{L"HaloMCCVR.dll",L"halo1.dll",L"halo2.dll",
        L"halo3.dll",L"halo3odst.dll",L"haloreach.dll",L"halo4.dll"};
    static HMODULE previous[7]{};
    for(size_t i=0;i<7;++i)
    {
        const HMODULE module=GetModuleHandleW(names[i]);
        if(module!=previous[i])
        {LOG("Native fault module map: %ls base=%p",names[i],module);previous[i]=module;}
    }
    for(auto& record:records)
        if(record.ready.load(std::memory_order_acquire) && !record.reported)
        {LOG("%s",record.text);record.reported=true;}
}
