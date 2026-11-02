#include "../src/common/bloom_override_logic.h"
#include <cstdlib>
#include <cstdio>
static void Check(bool value) { if(!value) std::abort(); }
int main()
{
    using namespace bloom_override;
    Check(Crc32("123456789",9)==0xCBF43926u);
    Check(kReachConsumer==2325652528u && kHalo3OdstConsumer==757649136u);
    const uint32_t hashes[]{kReachConsumer,kHalo3OdstConsumer,kOdstAutoExposure,0,12345};
    for(unsigned t=0;t<=unsigned(GameTitle::Unknown);++t)
        for(auto hash:hashes) for(int bloom=0;bloom<2;++bloom) for(int exposure=0;exposure<2;++exposure) {
            const auto title=static_cast<GameTitle>(t);
            auto expected=TogglePass::None;
            if(bloom&&((title==GameTitle::HaloReach&&hash==2325652528u)||
                (title==GameTitle::Halo3ODST&&hash==757649136u)))expected=TogglePass::Bloom;
            if(exposure&&title==GameTitle::Halo3ODST&&hash==3197137760u)expected=TogglePass::AutoExposure;
            Check(DisabledPass(title,hash,bloom,exposure)==expected);
        }
    std::puts("Post-effect policy: all title/hash/toggle combinations passed");
}
