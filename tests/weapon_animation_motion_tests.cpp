#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../src/common/weapon_animation_motion.h"
#include "../src/common/halo2_weapon_animation.h"
#include <cassert>
#include <limits>
using weapon_animation::Pose;
static bool near(float a,float b){return std::fabs(a-b)<.00001f;}
static void same(const Pose& a,const Pose& b)
{
    for(int i=0;i<9;++i)assert(near(a.rotation[i],b.rotation[i]));
    for(int i=0;i<3;++i)assert(near(a.translation[i],b.translation[i]));
}
int main()
{
    weapon_animation::Reference reference;
    std::array<uint64_t,4> key{1,2,3,4};Pose equip{},idle{},delta{};
    equip.translation[2]=.4f;equip.rotation[0]=0;equip.rotation[1]=1;
    equip.rotation[3]=-1;equip.rotation[4]=0;
    // A quiet pose may settle at the new 500ms initial minimum.
    for(uint64_t t=0;t<500;t+=100)assert(!reference.Update(key,idle,t,.33f,delta));
    assert(reference.Update(key,idle,500,.33f,delta));same(delta,Pose{});
    // Observed motion restarts the independent 500ms quiet window.
    weapon_animation::Reference movingReference;
    for(uint64_t t=0;t<300;t+=100)assert(!movingReference.Update(key,equip,t,.33f,delta));
    assert(!movingReference.Update(key,idle,300,.33f,delta));
    for(uint64_t t=400;t<800;t+=100)assert(!movingReference.Update(key,idle,t,.33f,delta));
    assert(movingReference.Update(key,idle,800,.33f,delta));same(delta,Pose{});
    Pose firing=idle;firing.translation[2]=.01f;
    assert(reference.Update(key,firing,600,.33f,delta));assert(near(delta.translation[2],0.f));
    // Recoil/reload never replace an established neutral reference.
    assert(reference.Update(key,equip,700,.33f,delta));
    assert(reference.Update(key,idle,800,.33f,delta));same(delta,Pose{});
    // Regrab and graphics/title identity changes settle independently.
    for(int change=0;change<4;++change) {
        ++key[change];assert(!reference.Update(key,equip,1900+change*2000,.33f,delta));
        for(uint64_t t=2000+change*2000;t<=uint64_t(3500+change*2000);t+=100)
            reference.Update(key,idle,t,.33f,delta);
        assert(reference.ready);same(delta,Pose{});
    }
    auto invalid=idle;invalid.translation[0]=std::numeric_limits<float>::quiet_NaN();
    assert(!reference.Update(key,invalid,10000,.33f,delta));assert(!reference.ready);
    // Multiple eyes at one timestamp cannot complete the settling window.
    for(int i=0;i<2000;++i)assert(!reference.Update(key,idle,10100,.33f,delta));

    Halo2CameraBasis camera{{2,3,4},{1,0,0},{0,0,1}};
    Pose root{},local{};local.translation[0]=.3f;
    assert(Halo2AnimationCameraPose(camera,root));
    auto world=weapon_animation::Compose(root,local);
    float packet[13]{};packet[0]=1;
    const auto write=[&](){for(int i=0;i<9;++i)packet[i+1]=world.rotation[i];for(int i=0;i<3;++i)packet[i+10]=world.translation[i];};
    write();reference={};
    for(uint64_t t=0;t<=1600;t+=100)Halo2ObserveWeaponAnimation(reference,key,packet,packet,camera,t,.33f,delta);
    assert(reference.ready);same(delta,Pose{});
    // Moving/turning the camera and its native gun together is not animation.
    camera.position[0]+=10;camera.forward[0]=0;camera.forward[1]=1;
    assert(Halo2AnimationCameraPose(camera,root));world=weapon_animation::Compose(root,local);write();
    assert(Halo2ObserveWeaponAnimation(reference,key,packet,packet,camera,1700,.33f,delta));same(delta,Pose{});
    local.translation[2]+=.01f;world=weapon_animation::Compose(root,local);write();
    assert(Halo2ObserveWeaponAnimation(reference,key,packet,packet,camera,1800,.33f,delta));
    assert(near(delta.translation[2],0.f));
    // Gun-only motion is already preserved by packet ownership; it must not
    // also become an extra wrist translation.
    float wrist[13];std::memcpy(wrist,packet,sizeof(wrist));
    packet[10]+=.1f;
    Pose independent{};
    assert(Halo2ObserveWeaponAnimation(reference,key,packet,wrist,camera,1850,.33f,independent));
    same(independent,delta);
    Halo2CameraBasis controller{{4,5,6},{1,0,0},{0,0,1}};
    assert(Halo2ApplyWeaponAnimation(controller,delta,1));assert(near(controller.position[2],6.f));

    delta.rotation[0]=delta.rotation[4]=std::cos(.2f);
    delta.rotation[1]=std::sin(.2f);delta.rotation[3]=-std::sin(.2f);
    delta.translation[0]=.4f;delta.translation[2]=.7f;
    assert(Halo2ApplyWeaponAnimation(controller,delta,1));
    assert(near(controller.position[0],4)&&near(controller.position[1],5)&&near(controller.position[2],6));
    assert(near(controller.forward[1],std::sin(.2f)));

    // Classic's multiple native camera passes can have different translations.
    // They must not starve a rotation-only settling window.
    reference={};
    for(uint64_t t=0;t<=1600;t+=100) {
        Pose moving{};moving.translation[0]=float(t)*.01f;
        reference.Update(key,moving,t,.33f,delta);
    }
    assert(reference.ready);same(delta,Pose{});
    const auto single=Halo2AnimationIdentity(1,2,3,4,5,UINT32_MAX);
    const auto dual=Halo2AnimationIdentity(1,2,3,4,5,6);
    const auto swapped=Halo2AnimationIdentity(1,2,3,4,5,7);
    assert(single!=dual&&dual!=swapped);
    reference={};
    for(uint64_t t=0;t<=1600;t+=100)reference.Update(single,Pose{},t,.33f,delta);
    assert(reference.ready);
    assert(!reference.Update(dual,Pose{},1700,.33f,delta));assert(!reference.ready);
    for(uint64_t t=1800;t<=3300;t+=100)reference.Update(dual,Pose{},t,.33f,delta);
    assert(reference.ready);
    assert(!reference.Update(single,Pose{},3400,.33f,delta));
    // Equivalent elapsed time must give the same angular response at 50/100Hz.
    const auto turn=[](float a) {
        Pose p;p.rotation[0]=p.rotation[4]=std::cos(a);
        p.rotation[1]=std::sin(a);p.rotation[3]=-std::sin(a);return p;
    };
    weapon_animation::RotationFilter slow,fast;
    Pose a{},b{},start{},target=turn(1.f);
    assert(slow.Update(key,start,0,a)&&fast.Update(key,start,0,b));
    for(uint64_t t=20;t<=200;t+=20)assert(slow.Update(key,target,t,a));
    for(uint64_t t=10;t<=200;t+=10)assert(fast.Update(key,target,t,b));
    same(a,b);assert(a.rotation[1]>.7f && a.rotation[1]<target.rotation[1]);
    const Pose firstEye=a;
    for(int i=0;i<100;++i)assert(slow.Update(key,target,200,a));
    same(a,firstEye);
    // Held recoil converges, never introduces translation, and stays orthonormal.
    target.translation[0]=100;
    for(uint64_t t=220;t<=10000;t+=20)assert(slow.Update(key,target,t,a));
    assert(near(a.rotation[1],std::sin(1.f))&&near(a.translation[0],0));
    auto nextKey=key;++nextKey[1];
    assert(slow.Update(nextKey,start,10020,a));same(a,start);
    target=turn(3.14159265f);
    assert(slow.Update(nextKey,target,10040,a));assert(std::fabs(a.rotation[1])>0);
    assert(slow.Update(nextKey,start,11000,a));same(a,start); // stale state resets
    target.rotation[0]=std::numeric_limits<float>::quiet_NaN();
    assert(!slow.Update(nextKey,target,11020,a)&&!slow.ready);

    // H4 reroot: camera/controller rotation and XYZ remain immediate; only the
    // authored local animation is damped. A new weapon cannot inherit the old pose.
    {
    weapon_animation::RotationFilter relativeFilter;
    Pose parent=turn(.3f),world=parent,result{};
    assert(weapon_animation::SmoothLocalRotation(relativeFilter,key,parent,world,0,result));
    parent=turn(1.2f);parent.translation[0]=50;
    world=weapon_animation::Compose(parent,turn(.5f));world.translation[2]=7;
    assert(weapon_animation::SmoothLocalRotation(relativeFilter,key,parent,world,20,result));
    assert(near(result.translation[0],50)&&near(result.translation[2],7));
    const auto local=weapon_animation::Relative(parent,result);
    assert(local.rotation[1]>0&&local.rotation[1]<std::sin(.5f));
    assert(weapon_animation::SmoothLocalRotation(relativeFilter,nextKey,parent,world,40,result));
    same(result,world);
    }

    // Even a previously cached reload pose must not bypass equip settling.
    weapon_animation::ReferenceCache cache;
    cache.Store(key,equip);
    weapon_animation::Reference cached;
    assert(!cached.Update(key,idle,0,.33f,delta,true,&cache));
    for(uint64_t t=100;t<500;t+=100)assert(!cached.Update(key,idle,t,.33f,delta,true,&cache));
    assert(cached.Update(key,idle,500,.33f,delta,true,&cache));same(delta,Pose{});
    cached={};
    assert(!cached.Update(key,idle,1700,.33f,delta,true,&cache));
    for(uint64_t t=1800;t<2200;t+=100)assert(!cached.Update(key,idle,t,.33f,delta,true,&cache));
    assert(cached.Update(key,idle,2200,.33f,delta,true,&cache));same(delta,Pose{});
    for(int field=0;field<4;++field) {
        auto changed=key;++changed[field];cached={};
        assert(!cached.Update(changed,idle,1900,.33f,delta,true,&cache));
    }
    weapon_animation::ReferenceCache unready;
    cached={};cached.Update(key,equip,0,.33f,delta,true,&unready);cached={};
    assert(!cached.Update(key,idle,100,.33f,delta,true,&unready));
    for(unsigned i=0;i<20;++i)cache.Store({99,i,0,0},idle);
    assert(!cache.Find(key,delta)); // bounded cache eviction cannot invent a reference

}
