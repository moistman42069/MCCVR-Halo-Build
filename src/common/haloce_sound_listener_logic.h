// HALO CE (halo1.dll): THE SOUND LISTENER WITNESS, the pure half. DEV BUILDS ONLY: src/dll/haloce_orientation.cpp
// includes this inside #if HALOMCCVR_DEV_DIAGNOSTICS. Nothing here changes anything the game reads.
//
// CE is not like Halo 2. Its sound manager (+0xB4CB80, E-CE-ORIENTATION-2) builds each listener from the
// OBSERVER's position, forward and up, exactly as Halo 2's does -- but in CE this mod does not write the head
// into the observer (the eyes are built in the renderer), so the game's own listener is the stock camera: the
// character's eye, looking where the body's control yaw looks. What turns it to the head is a HOOK: the
// "headset audio orientation" transaction (haloce_orientation.cpp, AudioHook) replaces the packet's forward and
// up, and only those, on its way into the sound backend, for listener 0, while the on-foot gameplay receipt
// admits. That hook's proof is offline (the pinned producer and backend, executed out of the game); the witness
// measures it in a running game, in the hook's own call:
//   * what the GAME built (the packet's matrix rows) against the head, the hand's aim and the stock camera;
//   * what was HANDED to the backend (the private head packet, or the game's own when the hook did not admit);
//   * how far the listener's position is from the head's -- the position is never changed, so with roomscale
//     this is the distance from the character's eye to where the player's head really is.
//
// THE PACKET'S MATRIX (halo_ce::AudioListenerPacket): scale +0x48, forward +0x4C, left +0x58, up +0x64, position
// +0x70. Its builder +0xBA24EC stores scale 1.0 at +0, its first vector at +4, (second x first) at +0x10 and its
// second vector at +0x1C; the producer hands it the observer's +0x20 and +0x2C and copies the observer's +0x00
// to +0x28 (tools/verify-halo2-sound-listener.py checks those stores on the pinned halo1.dll when it is given
// one). At run time each packet's rows must be a rotation of scale 1 whose second row is up x forward.
#pragma once

#include "haloce_audio_logic.h"
#include "haloce_first_person_logic.h"
#include "sound_listener_witness_logic.h"

#include <algorithm>
#include <cmath>

namespace halo_ce
{
// The head's centre camera in the game's world for one context: BuildEye's mapping without an eye's offset (the
// point BuildSightGeometry calls `head`), with the forward and up BuildHeadListener hands the backend.
inline bool BuildHeadCamera(const RenderContext& context, Camera& out) noexcept
{
    if (!context.tracking.serial || !context.tracking.generation || !context.tracking.spaceEpoch ||
        !context.referenceRevision || context.tracking.generation != context.reference.generation ||
        context.tracking.spaceEpoch != context.reference.spaceEpoch || !Valid(context.tracking.headOrientation) ||
        !Finite(context.reference.position) || !Finite(context.tracking.headPosition) ||
        !std::isfinite(context.unitsPerMeter) || context.unitsPerMeter <= 0 || context.unitsPerMeter > 10)
        return false;
    Camera frame{};
    Quat inverse{};
    if (!BuildTrackingFrame(context.camera, context.reference, frame, inverse))
        return false;
    const Quat head = Multiply(inverse, context.tracking.headOrientation);
    Vec3 delta = context.tracking.headPosition - context.reference.position;
    if (!Finite(delta))
        return false;
    delta = {std::clamp(delta.x, -4.0f, 4.0f), std::clamp(delta.y, -4.0f, 4.0f), std::clamp(delta.z, -4.0f, 4.0f)};
    if (!context.positional)
        delta = {};
    Camera candidate = context.camera;
    candidate.position = context.camera.position + ToNative(frame, Rotate(inverse, delta)) * context.unitsPerMeter;
    candidate.forward = ToNative(frame, Rotate(head, {0, 0, -1}));
    candidate.up = ToNative(frame, Rotate(head, {0, 1, 0}));
    if (!Finite(candidate.position) || !Finite(candidate.forward) || !Finite(candidate.up))
        return false;
    out = candidate;
    return true;
}

inline listener_witness::Pose WitnessPose(Vec3 position, Vec3 forward, Vec3 up) noexcept
{
    listener_witness::Pose pose;
    pose.position[0] = position.x;
    pose.position[1] = position.y;
    pose.position[2] = position.z;
    pose.forward[0] = forward.x;
    pose.forward[1] = forward.y;
    pose.forward[2] = forward.z;
    pose.up[0] = up.x;
    pose.up[1] = up.y;
    pose.up[2] = up.z;
    return pose;
}
// The listener a packet's matrix describes. False when the matrix is not a rotation of scale 1.
inline bool PacketListener(const AudioListenerPacket& packet, listener_witness::Pose& out) noexcept
{
    const listener_witness::Pose pose = WitnessPose(packet.matrixPosition, packet.matrixForward, packet.matrixUp);
    const float left[3]{packet.matrixLeft.x, packet.matrixLeft.y, packet.matrixLeft.z};
    if (!(packet.scale == 1.0f) || !listener_witness::Finite3(pose.position) ||
        !listener_witness::IsBlamBasis(pose.forward, left, pose.up))
        return false;
    out = pose;
    return true;
}

// One listener packet into a window. `native`: the packet the game built (a copy). `handed`: what goes to the
// backend when this mod's hook replaced it, else null. `context`: the gameplay context to take the head, the
// hand and the stock camera from, or null when there is none.
inline void WitnessPacket(listener_witness::Window& window, const AudioListenerPacket& native,
                          const AudioListenerPacket* handed, const RenderContext* context) noexcept
{
    using namespace listener_witness;
    Pose listener;
    if (!PacketListener(native, listener))
    {
        ++window.refused[static_cast<size_t>(Refusal::NotABasis)];
        return;
    }
    Pose head, camera, handedPose = listener;
    float aim[3]{};
    References refs;
    head_relative::Use use = head_relative::Use::NoSample;
    if (context)
    {
        camera = WitnessPose(context->camera.position, context->camera.forward, context->camera.up);
        refs.camera = &camera;
        Camera centre{};
        if (BuildHeadCamera(*context, centre))
        {
            head = WitnessPose(centre.position, centre.forward, centre.up);
            refs.head = &head;
            use = head_relative::Use::Ok;
        }
        else
            use = head_relative::Use::NoHeading;
        NodeMatrix hand{};
        if (BuildControllerMatrix(context->camera, context->tracking, context->reference,
                                  context->tracking.controllers.primaryAim, context->unitsPerMeter,
                                  context->positional, hand))
        {
            aim[0] = hand.forward.x;
            aim[1] = hand.forward.y;
            aim[2] = hand.forward.z;
            refs.aim = aim;
        }
        if (context->reference.seatHeadingValid)
            ++window.seated;
    }
    ++window.head[static_cast<size_t>(use)];
    // What reached the backend: the head packet when the hook replaced it, else the game's own.
    if (!handed || PacketListener(*handed, handedPose))
        refs.handed = &handedPose;
    AddSample(window, listener, refs);
    if (handed)
        ++window.handedHead;
    else
        ++window.handedStock;
}
} // namespace halo_ce
