#pragma once
#include <openxr/openxr.h>
#include <cmath>
#include <cstdint>

namespace game_menu_pointer {
inline XrVector3f RotatePoint(XrQuaternionf q, XrVector3f p) noexcept
{
    const XrVector3f t{2*(q.y*p.z-q.z*p.y),2*(q.z*p.x-q.x*p.z),2*(q.x*p.y-q.y*p.x)};
    return {p.x+q.w*t.x+q.y*t.z-q.z*t.y,
        p.y+q.w*t.y+q.z*t.x-q.x*t.z,p.z+q.w*t.z+q.x*t.y-q.y*t.x};
}
inline bool ValidPose(const XrPosef& p) noexcept
{
    const auto& q=p.orientation;
    const float norm=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;
    return std::isfinite(norm)&&std::fabs(norm-1)<.01f&&
        std::isfinite(p.position.x)&&std::isfinite(p.position.y)&&std::isfinite(p.position.z);
}
inline XrVector3f InversePoint(const XrPosef& p,XrVector3f v,bool direction=false) noexcept
{
    if(!direction) { v.x-=p.position.x;v.y-=p.position.y;v.z-=p.position.z; }
    return RotatePoint({-p.orientation.x,-p.orientation.y,-p.orientation.z,p.orientation.w},v);
}
inline bool RayHit(const XrCompositionLayerQuad& quad,const XrPosef& aim,
    const XrPosef& head,bool viewSpace,float& u,float& v) noexcept
{
    if(!ValidPose(quad.pose)||!ValidPose(aim)||(viewSpace&&!ValidPose(head))||
        !std::isfinite(quad.size.width)||!std::isfinite(quad.size.height)||
        quad.size.width<=0||quad.size.height<=0) return false;
    auto origin=aim.position;
    auto direction=RotatePoint(aim.orientation,{0,0,-1});
    if(viewSpace) { origin=InversePoint(head,origin);direction=InversePoint(head,direction,true); }
    origin=InversePoint(quad.pose,origin);direction=InversePoint(quad.pose,direction,true);
    if(origin.z<=0||direction.z>=-1e-5f) return false;
    const float t=-origin.z/direction.z;
    u=.5f+(origin.x+t*direction.x)/quad.size.width;
    v=.5f-(origin.y+t*direction.y)/quad.size.height;
    return std::isfinite(u)&&std::isfinite(v)&&u>=0&&u<=1&&v>=0&&v<=1;
}
// White ring with a dark border remains visible on light and dark menus.
inline uint32_t CursorPixel(int x,int y) noexcept
{
    const float dx=x-15.5f,dy=y-15.5f,r=dx*dx+dy*dy;
    if(r>225||r<36) return 0;
    return r>169||r<64 ? 0xFF101010u : 0xFFFFFFFFu;
}
inline XrCompositionLayerQuad CursorQuad(const XrCompositionLayerQuad& screen,
    XrSwapchain chain,float u,float v,bool pressed) noexcept
{
    auto q=screen;
    q.subImage.swapchain=chain;q.subImage.imageRect={{0,0},{32,32}};
    q.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    // Later in composition order, and lifted toward the viewer (a quad's +Z) by half a percent of the screen's width:
    // on the same plane a compositor that sorts by depth makes the ring fight the screen. From the panel's distance
    // the lift moves the ring against what it points at by far less than the ring's own width.
    const auto offset=RotatePoint(screen.pose.orientation,
        {(u-.5f)*screen.size.width,(.5f-v)*screen.size.height,screen.size.width*.005f});
    q.pose.position.x+=offset.x;q.pose.position.y+=offset.y;q.pose.position.z+=offset.z;
    q.size.width=q.size.height=screen.size.width*(pressed?.007f:.01f);
    return q;
}
}
