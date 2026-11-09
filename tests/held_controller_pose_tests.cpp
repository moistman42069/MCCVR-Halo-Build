#include "../src/common/held_controller_pose.h"
#include <cstdio>
#include <cstdlib>
#include <limits>

using held_controller_pose::Hold;
namespace
{
unsigned checks{};
void Check(bool ok,const char* why)
{++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
}

int main()
{
    Hold hold;
    float q[4]{},p[3]{};
    Check(!hold.Load(q,p,false),"nothing is held before the first tracked pose");

    const float q1[4]{0,0.7071f,0,0.7071f},p1[3]{0.2f,1.1f,-0.4f};
    hold.Store(q1,p1,false);
    Check(hold.Load(q,p,false),"the last tracked pose is held");
    Check(q[1]==q1[1]&&q[3]==q1[3]&&p[0]==p1[0]&&p[2]==p1[2],"the held pose is the one stored");
    Check(!hold.Load(q,p,true),"a weapon-hand swap drops the held pose");

    const float bad[3]{std::numeric_limits<float>::quiet_NaN(),0,0};
    hold.Store(q1,bad,false);
    Check(hold.Load(q,p,false)&&p[0]==p1[0],"a non-finite pose never replaces the held one");

    const float p2[3]{0.3f,1.2f,-0.5f};
    hold.Store(q1,p2,false);
    Check(hold.Load(q,p,false)&&p[0]==p2[0],"a newer tracked pose replaces the held one");
    hold.Clear();
    Check(!hold.Load(q,p,false),"a cleared hold holds nothing");
    std::printf("held_controller_pose_tests: %u checks passed\n",checks);
    return 0;
}
