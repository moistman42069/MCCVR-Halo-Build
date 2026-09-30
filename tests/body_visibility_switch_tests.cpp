// Execute the shipping legacy switch against guarded synthetic engine memory.
#include "../src/common/runtime_types.h"
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
unsigned checks=0,reads=0,writes=0;
void Check(bool ok,const char* why) {
    ++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}
}
uint8_t bytes[12]{},before[12]{};
uint8_t* failedRead=nullptr;
uint8_t* failedWrite=nullptr;
const char* missingName=nullptr;
GameTitle active=GameTitle::Halo3;
uint32_t observed=7;
std::atomic<uint32_t> g_halo3RuntimeGeneration{7};
struct {bool body_wip=false,experimental_body_ik=false;} g_config;
GameTitle TitleAdapter_GetActiveTitle(){return active;}
uint32_t TitleAdapter_GetGeneration(GameTitle){return observed;}
void* FindDebugVarSlot(uintptr_t,size_t,const char* name,uint64_t type) {
    Check(type==5,"body lookup requires native boolean type");
    if(missingName&&std::strcmp(missingName,name)==0)return nullptr;
    return bytes+(std::strcmp(name,"director_disable_first_person")==0?1:
                  std::strcmp(name,"render_first_person")==0?5:9);
}
int SafeReadByte(const uint8_t* p,uint8_t* out) {
    ++reads;if(p==failedRead)return 0;*out=*p;return 1;
}
int SafeWriteByte(uint8_t* p,uint8_t value) {
    ++writes;if(p==failedWrite)return 0;*p=value;return 1;
}
#define LOG(...) ((void)0)
#include "../src/dll/body_visibility_switch.inl"
#undef LOG
void Reset() {
    std::memset(bytes,0xa6,sizeof(bytes));bytes[1]=0;bytes[5]=1;bytes[9]=0;
    std::memcpy(before,bytes,sizeof(bytes));failedRead=failedWrite=nullptr;missingName=nullptr;
    active=GameTitle::Halo3;observed=7;g_halo3RuntimeGeneration=7;
    g_config={};ResolveBodyVars(0,sizeof(bytes));reads=writes=0;
}
}
int main() {
    Reset();g_config.body_wip=true;ApplyBodySetting();
    Check(bytes[1]==1&&bytes[5]==0&&bytes[9]==1,"legacy requested booleans apply");
    for(unsigned i=0;i<sizeof(bytes);++i)
        if(i!=1&&i!=5&&i!=9) Check(bytes[i]==before[i],"adjacent engine flags are never overwritten");
    g_config.body_wip=false;ApplyBodySetting();
    Check(std::memcmp(before,bytes,sizeof(bytes))==0,"disable restores exact original bytes");
    Reset();g_config.body_wip=true;g_config.experimental_body_ik=true;ApplyBodySetting();
    Check(writes==0&&!g_bodyApplied&&std::memcmp(before,bytes,sizeof(bytes))==0,
          "avatar enabled at startup preserves native FP producer despite saved legacy body preference");
    Check(g_config.body_wip,"avatar precedence does not erase the saved legacy body preference");
    g_config.experimental_body_ik=false;ApplyBodySetting();
    Check(g_bodyApplied&&bytes[1]==1&&bytes[5]==0&&bytes[9]==1,
          "disabling avatar reapplies the saved legacy body preference");
    g_config.experimental_body_ik=true;ApplyBodySetting();
    Check(!g_bodyApplied&&g_config.body_wip&&std::memcmp(before,bytes,sizeof(bytes))==0,
          "enabling avatar after legacy body restores every native FP switch byte");
    writes=0;ApplyBodySetting();
    Check(writes==0,"avatar precedence does not continually rewrite native flags");
    Reset();g_config.body_wip=true;++observed;ApplyBodySetting();
    Check(reads==0&&writes==0,"new title generation cannot access retained addresses");
    observed=7;g_halo3RuntimeGeneration=0;ApplyBodySetting();
    Check(reads==0&&writes==0,"retired camera cannot access retained addresses");
    g_halo3RuntimeGeneration=7;active=GameTitle::HaloReach;ApplyBodySetting();
    Check(reads==0&&writes==0,"another title cannot apply H3 switches");
    Reset();g_config.body_wip=true;failedRead=bytes+5;ApplyBodySetting();
    Check(writes==0&&g_bodyVarCount==0&&g_bodyNotice==3,"read fault refuses only the optional switch before writes");
    Reset();g_config.body_wip=true;failedWrite=bytes+5;ApplyBodySetting();
    Check(std::memcmp(before,bytes,sizeof(bytes))==0&&g_bodyVarCount==0&&!g_bodyApplied,
          "partial apply restores other original booleans after a guarded write failure");
    failedWrite=nullptr;writes=0;ApplyBodySetting();
    Check(writes==0,"faulted optional switch cannot keep retrying in camera hooks");
    Reset();missingName="director_disable_first_person";ResolveBodyVars(0,sizeof(bytes));
    writes=0;g_config.body_wip=true;ApplyBodySetting();
    Check(writes==0&&std::memcmp(before,bytes,sizeof(bytes))==0&&g_bodyVarCount==0,
          "incomplete switch set preserves first-person hands without partial activation");
    std::printf("PASS: %u body switch memory/lifecycle checks\n",checks);
}
