// HALO 3 GLASS FADE: how a vehicle camera's "fadeParts" makes a glass material show less of itself, by scaling the
// copy of its parameters the game makes for each draw. Pure: no game, engine or OpenXR includes.
//
// EVIDENCE CLASS: RETAIL-DERIVED (disassembly of halo3.dll's material bind +0x289B90, and the shader tags of the
// stock maps and a workshop map). Not tuned in a headset.
//
// A material's render method has one postprocess entry: its blend mode (+0x68: 0 opaque, 1 additive, 2 multiply,
// 3 alpha blend, 4 double multiply, 5 premultiplied alpha) and its real parameters, one float4 each, in the order
// its template names them (template +0x48: one string id per parameter). The bind copies them into a buffer of its
// own on every bind and lets the game's animated functions write into that copy; the copy is what the GPU gets.
// Fading writes only that copy, so nothing shared is changed and a failure leaves the stock look.
//
// WHAT FADING MEANS PER BLEND MODE (a glass is never opaque in these vehicles but the Banshee's, which cannot fade):
//   additive (Sparrowhawk, Hornet, Longsword glass): the glass ADDS light; every term that adds it is scaled by the
//     amount kept -- albedo_color, diffuse_coefficient, the three specular contributions and the environment tint.
//   multiply / double multiply (Sparrowhawk's glass_dark): the glass darkens what is behind it by its colour; that
//     colour moves toward the one that changes nothing (white; mid grey for double multiply).
//   alpha blend: its colour's alpha (how much of it covers what is behind) is scaled.
//   premultiplied alpha: colour and alpha are scaled together.
#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string_view>

namespace halo3_glass_fade
{
enum class Blend : int32_t
{
    Opaque = 0,
    Additive = 1,
    Multiply = 2,
    AlphaBlend = 3,
    DoubleMultiply = 4,
    PremultipliedAlpha = 5,
};

enum class Op : uint8_t
{
    None = 0,
    Scale,        // x, y, z and w times the amount kept
    TowardWhite,  // x, y, z toward 1
    TowardGray,   // x, y, z toward 0.5
    ScaleAlpha,   // w times the amount kept
};

inline constexpr int kMaxParameters = 32;

inline bool IsOneOf(std::string_view name, std::initializer_list<std::string_view> names)
{
    for (const std::string_view n : names)
        if (name == n)
            return true;
    return false;
}

// What fading does to the parameter called `name` of a material of blend mode `blend`.
inline Op OpFor(int32_t blend, std::string_view name)
{
    switch (static_cast<Blend>(blend))
    {
    case Blend::Additive:
        return IsOneOf(name, {"albedo_color", "diffuse_coefficient", "specular_coefficient",
                              "analytical_specular_contribution", "area_specular_contribution",
                              "environment_map_specular_contribution", "env_tint_color"})
                   ? Op::Scale
                   : Op::None;
    case Blend::Multiply:
        return name == "albedo_color" ? Op::TowardWhite : Op::None;
    case Blend::DoubleMultiply:
        return name == "albedo_color" ? Op::TowardGray : Op::None;
    case Blend::AlphaBlend:
        return name == "albedo_color" ? Op::ScaleAlpha : Op::None;
    case Blend::PremultipliedAlpha:
        return name == "albedo_color" ? Op::Scale : Op::None;
    default:
        return Op::None;
    }
}

inline const char* BlendName(int32_t blend)
{
    switch (static_cast<Blend>(blend))
    {
    case Blend::Opaque: return "opaque";
    case Blend::Additive: return "additive";
    case Blend::Multiply: return "multiply";
    case Blend::AlphaBlend: return "alpha blend";
    case Blend::DoubleMultiply: return "double multiply";
    case Blend::PremultipliedAlpha: return "premultiplied alpha";
    default: return "?";
    }
}

// One parameter (a float4) with `kept` of the glass left (0 = none of it, 1 = as the game draws it).
inline void Apply(Op op, float kept, float (&value)[4])
{
    if (!(kept >= 0.0f))
        kept = 0.0f;
    if (kept > 1.0f)
        kept = 1.0f;
    switch (op)
    {
    case Op::Scale:
        for (float& v : value)
            v *= kept;
        break;
    case Op::TowardWhite:
        for (int i = 0; i < 3; ++i)
            value[i] = 1.0f + (value[i] - 1.0f) * kept;
        break;
    case Op::TowardGray:
        for (int i = 0; i < 3; ++i)
            value[i] = 0.5f + (value[i] - 0.5f) * kept;
        break;
    case Op::ScaleAlpha:
        value[3] *= kept;
        break;
    default:
        break;
    }
}
} // namespace halo3_glass_fade
