#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <initializer_list>
#include "../src/common/runtime_types.h"
#include "../src/common/subtitle_logic.h"
using InterfaceHandoff=bool(__fastcall*)(void*,const void*,const void*,const void*,const wchar_t*,float,uint32_t);
static InterfaceHandoff g_originalInterface{};
#include "../src/dll/native_subtitle_authority.inl"
#include "../src/dll/native_subtitle_queue.inl"
static GameTitle title=GameTitle::HaloReach;
static uint32_t generation=7;
static bool theatre=false,result=true,fault=false,overwrite=false,retire=false,flip=false;
static unsigned checks=0,calls=0,captures=0;
static uint8_t expectedChannel=0;static bool expectedRefreshed=false;
static int self{},a{},b{},c{};
static wchar_t text[]=L"Localized native text";
static GameTitle TitleAdapter_GetActiveTitle(){return title;}
static uint32_t TitleAdapter_GetGeneration(GameTitle){return generation;}
static bool VR_IsCutsceneTheaterActive(){return theatre;}
static void Check(bool value,const char* reason){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",reason);std::exit(1);}}
static void NativeSubtitles_Capture(GameTitle t,uint32_t g,const wchar_t* str,float seconds,bool th,uint8_t channel,bool refreshed){
    ++captures;Check(t==title&&g==generation&&std::wcscmp(str,L"Localized native text")==0&&seconds==2.25f&&th==theatre&&
        channel==expectedChannel&&refreshed==expectedRefreshed,"capture preserves copied native payload and policy");
}
static bool __fastcall Original(void* s,const void* pa,const void* pb,const void* pc,const wchar_t* str,float seconds,uint32_t style){
    ++calls;
    Check(s==&self&&pa==&a&&pb==&b&&pc==&c&&str==text&&seconds==2.25f&&style==0xFEDCBA98,"all seven arguments preserved");
    if(fault)RaiseException(0xE0535458,0,0,nullptr);
    if(overwrite) text[0]=0;
    if(retire) SetSubtitleSources(title,0,nullptr,0);
    if(flip) theatre=!theatre;
    return result;
}
#include "../src/dll/native_subtitle_interface.inl"
static DWORD Invoke(uintptr_t caller,bool& returned){
    __try{returned=DispatchNativeSubtitleInterface(caller,&self,&a,&b,&c,text,2.25f,0xFEDCBA98);}
    __except(EXCEPTION_EXECUTE_HANDLER){return GetExceptionCode();}
    return 0;
}
static Event MakeEvent(const wchar_t* value,uint8_t channel,uint64_t sequence,uint64_t expires=500){
    Event e{};e.title=GameTitle::Halo4;e.generation=7;e.sequence=sequence;e.expires=expires;e.channel=channel;
    e.length=static_cast<uint32_t>(std::wcslen(value));std::wmemcpy(e.text,value,e.length+1);return e;
}
int main(){
    g_originalInterface=&Original;bool returned=false;
    const SubtitleSource sources[]={{123,0,false},{456,1,true},{789,2,true}};
    const GameTitle supported[]={GameTitle::Halo3,GameTitle::Halo3ODST,GameTitle::HaloReach,GameTitle::Halo4,GameTitle::Halo2};
    for(const auto current:supported){
        title=current;SetSubtitleSources(title,7,sources,_countof(sources));
        for(const bool presentation:{false,true}) {
            theatre=presentation;expectedChannel=0;expectedRefreshed=false;auto prior=captures;
            Check(Invoke(123,returned)==0&&returned&&captures==prior+1,"each title honors actual gameplay/theatre state");
            expectedChannel=1;expectedRefreshed=true;prior=captures;
            Check(Invoke(456,returned)==0&&returned&&captures==prior+1,"first native queue line");
            expectedChannel=2;prior=captures;
            Check(Invoke(789,returned)==0&&returned&&captures==prior+1,"second native queue line");
        }
    }
    const auto prior=captures;
    Check(Invoke(999,returned)==0&&captures==prior,"unproven caller stays native");
    result=false;Check(Invoke(456,returned)==0&&!returned&&captures==prior,"native visibility refusal retained");
    result=true;generation=8;Check(Invoke(456,returned)==0&&returned&&captures==prior,"stale generation rejected");
    generation=7;title=GameTitle::HaloCE;Check(Invoke(456,returned)==0&&captures==prior,"unimplemented CE source stays stock");
    title=GameTitle::Halo4;retire=true;Check(Invoke(456,returned)==0&&captures==prior,"retirement during native call rejects capture");
    retire=false;SetSubtitleSources(title,7,sources,_countof(sources));flip=true;
    Check(Invoke(456,returned)==0&&captures==prior,"presentation transition during call rejects capture");flip=false;
    fault=true;returned=false;const auto beforeCalls=calls;
    Check(Invoke(456,returned)==0xE0535458&&!returned&&captures==prior&&calls==beforeCalls+1,"native exception propagates without capture or retry");
    fault=false;overwrite=true;expectedChannel=1;expectedRefreshed=true;
    Check(Invoke(456,returned)==0&&returned&&captures==prior+1,"payload survives native text consumption");
    RasterCaption caption{};
    AcceptSubtitleEvent(MakeEvent(L"First|nline",1,1,300));AcceptSubtitleEvent(MakeEvent(L"Second",2,2,400));
    Check(ComposeSubtitleCaption(GameTitle::Halo4,7,false,200,caption)&&
        std::wcscmp(caption.text,L"First\nline\nSecond")==0&&caption.expires==300,"two native queue lines aggregate with independent lifetime");
    Check(ComposeSubtitleCaption(GameTitle::Halo4,7,false,350,caption)&&std::wcscmp(caption.text,L"Second")==0,"one expired line cannot remove live second line");
    AcceptSubtitleEvent(MakeEvent(L"Single",0,3));
    Check(ComposeSubtitleCaption(GameTitle::Halo4,7,false,350,caption)&&std::wcscmp(caption.text,L"Single")==0,"single branch clears both old queue lines");
    AcceptSubtitleEvent(MakeEvent(L"Unpaired",2,4));
    Check(ComposeSubtitleCaption(GameTitle::Halo4,7,false,350,caption)&&std::wcscmp(caption.text,L"Single")==0,"unpaired secondary is rejected");
    Check(!ComposeSubtitleCaption(GameTitle::Halo4,8,false,350,caption),"generation transition clears queued lines");
    auto longest=MakeEvent(L"x",1,5);longest.length=1023;
    for(unsigned i=0;i<1023;++i)longest.text[i]=L'x';longest.text[1023]=0;
    AcceptSubtitleEvent(longest);longest.channel=2;longest.sequence=6;AcceptSubtitleEvent(longest);
    Check(ComposeSubtitleCaption(GameTitle::Halo4,7,false,350,caption)&&caption.length==2047&&caption.text[2047]==0,"two maximum native strings remain bounded");
    Check(!ComposeSubtitleCaption(GameTitle::Halo4,7,true,350,caption),"gameplay captions cannot survive theatre transition");
    std::printf("PASS: %u native subtitle ABI, authority and queue checks\n",checks);
}
