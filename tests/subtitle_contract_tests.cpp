#include "../src/common/subtitle_logic.h"
#include <cstdio>
#include <cwchar>
#include <limits>

static int failures=0;
static void Check(bool ok,const char* reason) { if(!ok){std::fprintf(stderr,"FAIL: %s\n",reason);++failures;} }
int main() {
    wchar_t out[1024]{};
    const wchar_t localized[]=L"Allons-y! |n\x65e5\x672c\x8a9e";
    Check(subtitles::Normalize(localized,std::wcslen(localized),out,1024),"localized text admitted");
    Check(std::wcscmp(out,L"Allons-y! \n\x65e5\x672c\x8a9e")==0,"authored line break and Unicode preserved");
    const wchar_t header[]=L"cor&idCORTANA\xE900" L"Follow me.";
    Check(subtitles::Normalize(header,std::wcslen(header),out,1024),"verified speaker header admitted");
    Check(std::wcscmp(out,L"CORTANA: Follow me.")==0,"speaker retained without engine identifier");
    const wchar_t surrogate[]={0xD83D,0xDE00,0};
    Check(subtitles::Normalize(surrogate,2,out,1024),"UTF16 surrogate pair retained");
    Check(!subtitles::Normalize(surrogate,1,out,1024),"truncated surrogate rejected");
    const wchar_t low[]={0xDE00,0};
    Check(!subtitles::Normalize(low,1,out,1024),"orphan surrogate rejected");
    Check(!subtitles::Normalize(L" ",1,out,1024),"blank caption rejected");
    Check(!subtitles::Normalize(L"a|z",3,out,1024),"unknown authored control rejected");
    Check(!subtitles::Normalize(L"a\xE900",2,out,1024),"unidentified private-use markup rejected");
    Check(!subtitles::Normalize(L"text",4,out,4),"missing terminator capacity rejected");
    Check(subtitles::DurationMs(0.001f)==2 || subtitles::DurationMs(0.001f)==1,"positive subframe duration bounded");
    Check(subtitles::DurationMs(2.5f)==2500,"native duration retained");
    Check(!subtitles::DurationMs(-1.f) && !subtitles::DurationMs(0.f),"invalid durations rejected");
    Check(!subtitles::DurationMs(std::numeric_limits<float>::quiet_NaN()),"NaN duration rejected");
    Check(!subtitles::DurationMs(std::numeric_limits<float>::infinity()),"infinite duration rejected");
    Check(!subtitles::DurationMs(601.f),"unbounded duration rejected");
    Check(subtitles::Current(GameTitle::Halo3,7,100,GameTitle::Halo3,7,99),"matching title generation live");
    Check(!subtitles::Current(GameTitle::Halo3,7,100,GameTitle::Halo3ODST,7,99),"title leakage rejected");
    Check(!subtitles::Current(GameTitle::HaloReach,7,100,GameTitle::HaloReach,8,99),"reentry leakage rejected");
    Check(!subtitles::Current(GameTitle::Halo3,7,100,GameTitle::Halo3,7,100),"native expiration exact");
    Check(!subtitles::Current(GameTitle::None,0,100,GameTitle::None,0,99),"unowned caption rejected");
    return failures?1:0;
}
