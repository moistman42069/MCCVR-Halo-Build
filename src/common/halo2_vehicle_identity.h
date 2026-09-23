#pragma once
#include <cmath>
#include <cstdint>
#include <cstddef>
namespace halo2_vehicle_identity {
struct Model {uint64_t identity;const char* name;unsigned nodes;float bounds[6];};
// Generated from all 79 official H2EK vehicle-subtree render-model exports.
// Bounds are matched only with the native model node count; ambiguous matches stay unknown.
inline constexpr Model kModels[]={
    {0x500C52F8DDCD350Full,"Banshee",4,{-1.07421f,0.770313f,-1.133237f,1.135983f,-0.005226f,1.58438f}},
    {0xB256630479EA5982ull,"Banshee / Cab Garbage",1,{-0.637979f,0.573652f,-0.334639f,0.327206f,-0.132353f,0.433543f}},
    {0x9CF71CFA2B7C768Aull,"Banshee / Pod Garbage",1,{-0.22298f,0.232631f,-0.375205f,0.343805f,-0.015752f,0.209255f}},
    {0x20A759CDE55782D4ull,"Banshee / Rudder Garbage",1,{-0.236528f,0.220209f,-0.282462f,0.30523f,-0.037228f,0.10229f}},
    {0xA54BB5283616D99Bull,"C Turret Ap",8,{-0.339521f,0.53416f,-0.29455f,0.305253f,-0.0f,0.702204f}},
    {0x569C5AB83E976BC6ull,"C Turret Ap / Gun",1,{-0.207693f,0.159655f,-0.126824f,0.097235f,0.000349f,0.124633f}},
    {0x4EC14631FE4C7CD2ull,"C Turret Ap / Seat",1,{-0.200574f,0.211067f,-0.049284f,0.063829f,-0.000386f,0.126312f}},
    {0x883537BFF3B3D038ull,"C Turret Ap / Turret",1,{-0.116291f,0.111351f,-0.184436f,0.186769f,-0.000672f,0.105437f}},
    {0x733C5A1CCA0B20E9ull,"Civilian / Bus",1,{-1.724982f,1.72421f,-0.7183f,0.539329f,-0.002035f,1.228112f}},
    {0x72C46ACC7DEA6671ull,"Civilian / Civ Wheel",1,{-0.151449f,0.151449f,-0.151449f,0.151449f,-0.053898f,0.053898f}},
    {0x11A46BCBA19870BBull,"Civilian / Mlx Door",1,{-0.235646f,0.235646f,-0.070237f,0.070237f,-0.188967f,0.188967f}},
    {0x7E3504AE7804B25Bull,"Civilian / Mlx Tire",1,{-0.16f,0.16f,-0.065f,0.065f,-0.157569f,0.157569f}},
    {0x57634E1E27BFF785ull,"Civilian / Mlx",1,{-0.759105f,0.793084f,-0.523428f,0.502852f,0.013121f,0.571581f}},
    {0xC5C6E784E70A17C5ull,"Civilian / Panel Truck",1,{-1.124459f,1.089241f,-0.530565f,0.590736f,-0.001173f,1.118906f}},
    {0xAA997BAB8FCECF01ull,"Civilian / Uberchassis",1,{-0.823152f,0.844967f,-0.435709f,0.442194f,-0.006632f,0.573669f}},
    {0x00727ED5F3412504ull,"Civilian / Uberchassis Tire",1,{-0.045267f,0.042974f,-0.146828f,0.143201f,-0.145896f,0.141265f}},
    {0xA93E6BADB475A0B9ull,"Cov Guntower",5,{-4.006669f,2.560468f,-3.2232f,3.274731f,-0.505059f,6.012204f}},
    {0x4C898A8676DD45F7ull,"Creep",6,{-2.138928f,2.431309f,-1.342741f,1.342741f,-0.000231f,2.267829f}},
    {0x957587A2130C86F3ull,"Falcon",7,{-1.821834f,2.074466f,-1.16149f,1.16149f,0.161368f,1.182552f}},
    {0x8166A4762CD885B5ull,"Ghost / Hull Shell",1,{-0.312895f,0.377945f,-0.235531f,0.288892f,-0.036953f,0.288172f}},
    {0x488401744AFEB1A7ull,"Ghost / L Wing Shell",1,{-0.194287f,0.187762f,-0.159271f,0.24586f,0.003131f,0.125263f}},
    {0x168EAB571702F1E7ull,"Ghost / R Wing Shell",1,{-0.190922f,0.165344f,-0.175823f,0.173511f,-0.015316f,0.161921f}},
    {0xC18FF82F4D440A57ull,"Ghost / Seat",1,{-0.26038f,0.231671f,-0.186435f,0.219235f,-0.096668f,0.258015f}},
    {0x62A1E1CCAEA5BB3Full,"Ghost",5,{-0.440746f,0.932158f,-0.591093f,0.591093f,-0.151515f,0.444909f}},
    {0xAEC9126F3BE73627ull,"Gravity Throne",7,{-0.184464f,0.399097f,-0.244482f,0.244463f,0.1219f,0.605534f}},
    {0xF47D815C06F524F7ull,"H Turret Ap",4,{-0.224622f,0.282386f,-0.224622f,0.224622f,0.0f,0.5803f}},
    {0xF2C4E6F99E9D00F3ull,"Insertion Pod",3,{-0.328178f,0.328178f,-0.312116f,0.312116f,-0.518619f,1.106506f}},
    {0x30FD5D7E429CF6B7ull,"Insertion Pod / Insertion Pod Lower Door",1,{-0.196723f,0.087852f,-0.312116f,0.312116f,0.0f,0.820382f}},
    {0xAD641DE8AF07BF65ull,"Insertion Pod / Insertion Pod Upper Door",1,{-0.133645f,0.110786f,-0.312116f,0.312116f,-0.352315f,0.018946f}},
    {0x1012CD025BC1E2E7ull,"Longsword",3,{-6.291167f,4.216159f,-6.147777f,6.147777f,0.031808f,2.146212f}},
    {0xDCDB06A306A6FE9Bull,"Mongoose",13,{-0.48181f,0.487904f,-0.304716f,0.304716f,-0.002721f,0.437251f}},
    {0x7666AA5DE95E8C28ull,"Pelican / Crashed",1,{-5.084844f,5.053671f,-3.6306f,3.565292f,-1.476992f,1.541592f}},
    {0xE8CAA1D915164CC7ull,"Pelican / Engine Nacelle Left",1,{-1.111489f,1.104338f,-0.568065f,0.421002f,-0.605815f,0.368698f}},
    {0x2D77DF01BAADADB1ull,"Pelican / Engine Nacelle Right",1,{-1.375205f,0.821089f,-0.515291f,0.441502f,-0.686899f,0.277507f}},
    {0xF1E3D7A37249A2F5ull,"Pelican / Landing Strut Left",1,{-1.39181f,1.178822f,-0.222256f,0.219936f,-0.422586f,0.29275f}},
    {0x73D6A970C11D0EB7ull,"Pelican / Landing Strut Right",1,{-1.366438f,1.216843f,-0.234868f,0.113384f,-0.305865f,0.229548f}},
    {0xF6C46B476449A913ull,"Pelican",20,{-5.012177f,5.017046f,-3.84222f,3.84222f,-0.28933f,3.299556f}},
    {0x605868ACBE554056ull,"Pelican / Pelican Rear Gun",4,{-0.208849f,0.282441f,-0.118482f,0.050314f,-0.380928f,0.183735f}},
    {0x48241B6524E79BECull,"Pelican / Pelican Rocket Pod",2,{-0.50465f,0.682119f,-0.20995f,0.20995f,-0.633291f,0.025056f}},
    {0xC08E9E8B67822FA3ull,"Phantom / Cinematics",1,{-2.680683f,3.868864f,-1.760538f,1.760538f,0.703045f,3.150606f}},
    {0xF6780162D5F36A87ull,"Phantom",4,{-5.794595f,4.826123f,-3.342243f,3.342243f,0.023522f,3.938239f}},
    {0x20E5C614B024D329ull,"Phantom / Chin Gun",2,{-0.052667f,1.937095f,-0.272485f,0.272485f,-0.405608f,0.096605f}},
    {0x987ABA07E75F631Full,"Phantom / Gun",1,{-0.491155f,0.523473f,-0.296942f,0.286798f,-0.142736f,0.164772f}},
    {0x20081D362CCF3E21ull,"Scorpion / Engine Panel",1,{-0.101642f,0.104007f,-0.210996f,0.21063f,-0.194735f,0.195615f}},
    {0x15C59B648110F03Dull,"Scorpion / L Big Panel",1,{-0.050846f,0.087484f,-0.127383f,0.171498f,-0.285797f,0.298326f}},
    {0xDB07DDE35E244751ull,"Scorpion / L Tread Cover",1,{-0.244414f,0.238904f,-0.399424f,0.407859f,-0.705659f,0.447146f}},
    {0xB73538311BFB7A51ull,"Scorpion / R Tread Cover",1,{-0.217021f,0.206242f,-0.436918f,0.226784f,-0.663893f,0.642939f}},
    {0x1E6E302886CF3D7Bull,"Scorpion",7,{-1.630645f,1.680304f,-1.249179f,1.279297f,-0.008893f,0.806335f}},
    {0x514718CA510F8A29ull,"Scorpion / Cannon",4,{-0.839774f,2.235594f,-0.479536f,0.479536f,-0.016355f,0.652737f}},
    {0xECD9D562B2E05247ull,"Scorpion / Panel",1,{-0.058111f,0.101717f,-0.205269f,0.196677f,-0.234328f,0.226819f}},
    {0x2BC7A6D8C5851AE1ull,"Scorpion / Turret",2,{-0.015697f,0.744535f,-0.534059f,0.392397f,-2.201826f,0.785315f}},
    {0x08AC12B3A5201966ull,"Spectre / Bar Garbage",1,{-0.259058f,0.207017f,-0.060694f,0.084501f,-0.025164f,0.130615f}},
    {0xCD5EA0B12F0AFB6Aull,"Spectre / Cab Garbage",1,{-0.473308f,0.291513f,-0.256253f,0.288309f,-0.047801f,0.206207f}},
    {0xA93196BBD057C3CCull,"Spectre / Dash Garbage",1,{-0.202334f,0.181733f,-0.111802f,0.190992f,-0.026439f,0.1777f}},
    {0x9C20005F57A280F0ull,"Spectre / Gun Base Garbage",1,{-0.402441f,0.469836f,-0.317174f,0.358797f,-0.074025f,0.520539f}},
    {0x9A19EC3A07FD8832ull,"Spectre / Pod Garbage",1,{-0.271857f,0.323973f,-0.290292f,0.422952f,-0.059508f,0.303777f}},
    {0x77457AB7A7E24F66ull,"Spectre / Wing Garbage",1,{-0.387954f,0.36034f,-0.205371f,0.186043f,-0.059014f,0.20899f}},
    {0x9A3C20811124D7B3ull,"Spectre",7,{-1.602507f,0.515653f,-0.829551f,0.832015f,-0.003861f,1.087596f}},
    {0xBCDBBE177F9F8348ull,"Spectre / Plasma",5,{-0.312872f,0.654776f,-0.335807f,0.381702f,-0.086767f,0.633318f}},
    {0x7A28EEA7A67B1EEEull,"Warthog / Bumper",1,{-0.042291f,0.035653f,-0.292732f,0.291483f,-0.049413f,0.042738f}},
    {0xC9E502950E28E2CAull,"Warthog / Hubcap",1,{-0.096376f,0.096376f,-0.095522f,0.097229f,-0.017372f,0.017372f}},
    {0xDF69FEFB2ADEBFFAull,"Warthog / Lb Fender",1,{-0.254835f,0.254835f,-0.045841f,0.045841f,-0.049209f,0.049209f}},
    {0x122D27C900A3115Aull,"Warthog / Lf Fender",1,{-0.204915f,0.204915f,-0.149024f,0.149024f,-0.0874f,0.0874f}},
    {0x66674CF126B91802ull,"Warthog / Rb Fender",1,{-0.254835f,0.254835f,-0.048419f,0.04842f,-0.052605f,0.052605f}},
    {0x6B0FA7C697103D72ull,"Warthog / Rf Fender",1,{-0.204915f,0.204915f,-0.149023f,0.149024f,-0.090354f,0.090354f}},
    {0xD5F9E006676B7C92ull,"Warthog / Sailpanel",1,{-0.183472f,0.183666f,-0.090194f,0.109904f,-0.025907f,0.044901f}},
    {0x0D975E94658BA2A8ull,"Warthog / Tire",1,{-0.199633f,0.199633f,-0.190697f,0.190697f,-0.119933f,0.119933f}},
    {0x5ECB6491547D19CEull,"Warthog / Winch",1,{-0.069138f,0.069138f,-0.155239f,0.150937f,-0.062785f,0.062785f}},
    {0xE078652292F8575Eull,"Warthog / Chaingun",5,{-0.22f,0.74533f,-0.22f,0.22f,-0.023906f,0.630392f}},
    {0x37169438BE3C9B5Aull,"Warthog / Shield Garbage",1,{-0.157697f,0.154827f,-0.160424f,0.164232f,-0.002672f,0.062891f}},
    {0xAEBBD4C86F34217Aull,"Warthog / Gauss",6,{-0.22f,0.745326f,-0.22f,0.22f,-0.023906f,0.697661f}},
    {0x0F2F9DDD9BC289A3ull,"Warthog",15,{-0.996299f,0.984343f,-0.51156f,0.503887f,-0.018016f,0.760923f}},
    {0x5D209ED64057708Bull,"Wraith / Mortar Door",1,{-0.176912f,0.252092f,-0.559885f,0.708596f,0.018094f,0.222537f}},
    {0xB623D03253DD5641ull,"Wraith / Mortar Hatch",1,{-0.071541f,0.07323f,-0.208146f,0.232949f,-0.127415f,0.260633f}},
    {0xEBA331083738BAD1ull,"Wraith / Rudder",1,{-0.106289f,0.540662f,-0.44728f,0.289528f,-0.025507f,0.200277f}},
    {0x4F4027C7BE461F73ull,"Wraith / Wing Boost",1,{-0.066113f,0.383545f,-0.24588f,0.18226f,-0.070882f,0.229103f}},
    {0x8D37786250BD8497ull,"Wraith / Minigun",2,{-0.07f,0.180977f,-0.074532f,0.075522f,-0.004789f,0.102822f}},
    {0x84936C19FD9580D7ull,"Wraith / Mortar",9,{-1.077667f,0.562917f,-0.587213f,0.615609f,-0.353858f,0.632163f}},
    {0x55D8FE797FA258C7ull,"Wraith",8,{-1.953192f,0.919484f,-1.555637f,1.521438f,0.02996f,0.991505f}},
};
inline const Model* Find(const float* bounds,unsigned nodes) noexcept {
    if(!bounds||!nodes||nodes>255) return nullptr;
    for(unsigned i=0;i<6;++i) if(!std::isfinite(bounds[i])) return nullptr;
    for(unsigned i=0;i<6;i+=2) if(bounds[i]>bounds[i+1]) return nullptr;
    const Model* found=nullptr;
    for(const auto& model:kModels) {
        if(model.nodes!=nodes) continue;
        bool match=true;
        for(unsigned i=0;i<6;++i) match=match&&std::fabs(bounds[i]-model.bounds[i])<=.000002f;
        if(match) {if(found) return nullptr;found=&model;}
    }
    return found;
}
inline const char* Name(uint64_t identity) noexcept {
    for(const auto& model:kModels) if(model.identity==identity) return model.name;
    return nullptr;
}
}
