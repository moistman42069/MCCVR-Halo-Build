#include "../src/common/setting_search.h"
#include <cstdio>
#include <cstdlib>
static unsigned checks{};
static void Check(bool value,const char* label)
{++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",label);std::exit(1);}}
static bool Finds(const char* query,std::initializer_list<std::string_view> fields)
{return setting_search::Matches(setting_search::Parse(query),fields);}
int main()
{
    // A setting: its own name, then its section and its page.
    const std::initializer_list<std::string_view> keepWalking{"Keep walking until I let go of the stick","D-pad gesture (hold controller next to head)","Controls"};
    Check(Finds("walk",keepWalking),"a part of a word finds it");
    Check(Finds("STICK walk",keepWalking),"words in any order, any case");
    Check(Finds("dpad walk",keepWalking)==false,"every word must be found");
    Check(Finds("d-pad walk",keepWalking),"a word can come from the section");
    Check(Finds("controls stick",keepWalking),"or from the page");
    Check(Finds("\"go of the\"",keepWalking),"a phrase is matched as one");
    Check(Finds("\"the go\"",keepWalking)==false,"a phrase keeps its order");
    Check(Finds("walk -stick",keepWalking)==false,"an excluded word rules it out");
    Check(Finds("walk !melee",keepWalking),"an excluded word that is absent leaves it in");
    Check(Finds("melee OR walk",keepWalking)&&Finds("melee | walk",keepWalking),"either side of OR");
    Check(Finds("melee OR punch",keepWalking)==false,"neither side of OR");
    Check(Finds("+controls",keepWalking)&&Finds("+control",keepWalking)==false,"+ matches a whole field");
    Check(Finds("keep...",keepWalking)&&Finds("walking...",keepWalking)==false,"... after a word: a field starting with it");
    Check(Finds("...stick",keepWalking)&&Finds("...keep",keepWalking)==false,"... before a word: a field ending with it");
    Check(Finds("",keepWalking)==false&&Finds("   ",keepWalking)==false,"nothing typed, nothing found");
    Check(Finds("walk AND stick",keepWalking),"AND is the same as a space");
    Check(Finds("-melee",keepWalking),"only exclusions: everything else");
    std::printf("setting_search: %u checks passed\n",checks);
}
