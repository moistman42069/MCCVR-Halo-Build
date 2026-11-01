#pragma once

// HALO 4 COPIES ITS FINISHED PICTURE TO DRAW IT AGAIN.
//
// Some of Halo 4's full-screen HUD materials take the frame itself as a texture: the visor's warp when the
// player is hurt with shields down is one (a `color_map` of the frame and a `warp_map`), drawn over the whole
// target with no blending. To have the frame as a texture the game first copies its scene colour target into a
// texture of its own, whole, then draws that copy back.
//
// While an eye is drawn, the draws the game aims at the scene colour target go into that eye's image instead
// (vr.cpp, the output-merger redirect), so the texture behind the target still holds an older picture: the last
// eye written back to it. Copied as it stands, the game pastes that old picture over the eye in every frame the
// effect lasts. The old picture already carries what the HUD blended on top of it (the damage flash), the HUD
// blends it on again, and the result is written back to be the next frame's old picture: the view dims frame
// after frame until it is black, for as long as the effect runs.
//
// So such a copy is taken from the eye's image: the picture the game means when it says "the frame".
namespace halo4_scene_copy
{
// True: a whole-resource copy the game makes is to read the eye's image in place of its scene colour texture.
//   sourceIsSceneColour       the copy's source is the texture behind the learned scene colour target
//   eyeRasterOnThisContext    an eye is being drawn now, on this thread and this device context
//   eyeDrawsRedirected        that eye's scene colour draws have gone into its image (it holds this eye's frame)
//   eyeImageExists            the eye's image exists
//   destinationIsThatEyeImage the copy would then read and write one texture: left alone
constexpr bool TakesEyeImage(bool enabled, bool halo4Active, bool sourceIsSceneColour, bool eyeRasterOnThisContext,
                             bool eyeDrawsRedirected, bool eyeImageExists, bool destinationIsThatEyeImage) noexcept
{
    return enabled && halo4Active && sourceIsSceneColour && eyeRasterOnThisContext && eyeDrawsRedirected &&
           eyeImageExists && !destinationIsThatEyeImage;
}
}
