#include "../src/common/fp_arm_fallback_logic.h"
#include <cstdio>
#include <cstdlib>

using namespace fp_arm_fallback;
namespace
{
unsigned checks{};
void Check(bool ok,const char* why)
{++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
}

int main()
{
    int bankA{},bankB{};
    ThreadTrace trace{};
    Check(Classify(true,true,trace,&bankA,1)==Outcome::Posed,"a matched, solved palette is posed");
    Check(Classify(true,false,trace,&bankA,1)==Outcome::SolveFailed,"a matched, unsolved palette failed its solve");
    Check(Classify(false,false,trace,&bankA,1)==Outcome::NoInterpolation,"nothing interpolated on this thread");

    trace.interpolations=1;trace.lastResult=false;
    Check(Classify(false,false,trace,&bankA,1)==Outcome::InterpolationFailed,"the game's interpolation gave no bones");
    trace.lastResult=true;
    Check(Classify(false,false,trace,&bankA,1)==Outcome::ContextRejected,"bones the arm solve does not know");
    trace.lastContextBuilt=true;
    Check(Classify(false,false,trace,&bankA,1)==Outcome::SourceMismatch,"another bank was interpolated");

    // A bank used once this frame: drawing it again in the same frame is a repeat, not a fallback.
    trace={};trace.lastConsumed=&bankA;trace.consumedFrame=7;
    Check(Classify(false,false,trace,&bankA,7)==Outcome::RepeatSubmission,"same bank, same frame");
    Check(Classify(false,false,trace,&bankB,7)==Outcome::NoInterpolation,"another bank, nothing interpolated");
    Check(Classify(false,false,trace,&bankA,8)==Outcome::NoInterpolation,"same bank, a later frame, no interpolation");
    trace.interpolations=1;trace.lastResult=true;trace.lastContextBuilt=true;
    Check(Classify(false,false,trace,&bankA,7)==Outcome::SourceMismatch,"an interpolation in between is not a repeat");

    for(int i=0;i<static_cast<int>(Outcome::Count);++i)
        Check(Name(static_cast<Outcome>(i))[0]!='?',"every outcome has a name");
    std::printf("fp_arm_fallback_tests: %u checks passed\n",checks);
    return 0;
}
