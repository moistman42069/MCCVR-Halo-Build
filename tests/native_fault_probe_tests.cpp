#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>
static unsigned checks{},reports{};
static void Check(bool good,const char* label)
{++checks;if(!good){std::fprintf(stderr,"FAIL: %s\n",label);std::exit(1);}}
void Logf(const char* format,...)
{if(std::string(format)=="%s")++reports;}
// Include production diagnostics so synthetic saved contexts can test the
// observer without dereferencing an invalid address in the test process.
#include "../src/dll/native_fault_probe.cpp"
static DWORD RaiseNative()
{
    ULONG_PTR args[]{0,0x1234};
    __try{RaiseException(EXCEPTION_ACCESS_VIOLATION,0,2,args);}
    __except(EXCEPTION_EXECUTE_HANDLER){return GetExceptionCode();}
    return 0;
}
static std::string Read(const std::filesystem::path& path)
{std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
int main()
{
    wchar_t temporary[MAX_PATH]{};Check(GetTempPathW(MAX_PATH,temporary)>0,"temporary directory");
    const auto directory=std::filesystem::path(temporary)/(L"HaloMCCVR-fault-test-"+std::to_wstring(GetCurrentProcessId()));
    Check(std::filesystem::create_directory(directory),"isolated probe directory");
    const auto path=directory/L"HaloMCCVR-native-faults.log";
    {std::ofstream old(path);old<<"previous evidence";}
    Check(NativeFaultProbe_Init((directory.wstring()+L"\\").c_str(),"probe-test-source"),"observer installed");
    Check(Read(path.wstring()+L".prev")=="previous evidence","prior evidence preserved");
    EXCEPTION_RECORD record{};CONTEXT context{};EXCEPTION_POINTERS pointers{&record,&context};
    record.ExceptionCode=EXCEPTION_ACCESS_VIOLATION;record.ExceptionAddress=reinterpret_cast<void*>(0x12345000);
    record.NumberParameters=2;record.ExceptionInformation[0]=1;record.ExceptionInformation[1]=0xBAD;
    context.Rip=0x12345000;context.Rcx=0xABC;context.Rsp=0xDEF;
    SetLastError(0x4321);
    Check(Observe(&pointers)==EXCEPTION_CONTINUE_SEARCH&&GetLastError()==0x4321,"observer preserves native handling and last error");
    const std::string first=Read(path);
    Check(first.find("probe-test-source")!=std::string::npos&&first.find("0x0000000012345000")!=std::string::npos&&
        first.find("0x0000000000000BAD")!=std::string::npos&&first.find("0x0000000000000ABC")!=std::string::npos,"exact source, address, access target and registers persisted before worker poll");
    Observe(&pointers);Check(Read(path)==first,"repeated fault site bounded");
    record.ExceptionAddress=reinterpret_cast<void*>(0x56789000);record.ExceptionCode=0xE06D7363;
    Check(Observe(&pointers)==EXCEPTION_CONTINUE_SEARCH&&Read(path)==first,"ordinary C++ exceptions ignored");
    std::atomic<unsigned> caught{};
    std::vector<std::thread> threads;
    for(int i=0;i<8;++i)threads.emplace_back([&]{if(RaiseNative()==EXCEPTION_ACCESS_VIOLATION)++caught;});
    for(auto& thread:threads)thread.join();
    Check(caught==8,"real Windows dispatch reaches all original handlers, no suppression or replay");
    const std::string concurrent=Read(path);
    size_t count=0,at=0;while((at=concurrent.find("END FAULT",at))!=std::string::npos){++count;at+=9;}
    Check(count==2,"concurrent same-site faults publish one complete record");
    NativeFaultProbe_Poll();Check(reports==2,"worker mirrors completed fault records to normal log");
    // Tests alone remove the observer, after all faulting threads have joined.
    RemoveVectoredExceptionHandler(handler);CloseHandle(file);
    std::filesystem::remove(path);std::filesystem::remove(path.wstring()+L".prev");std::filesystem::remove(directory);
    std::printf("PASS: %u native exception observation checks\n",checks);
}
