#pragma once

#include "../common/title_registry.h"
#include "../common/title_runtime_state.h"
#include "../common/cutscene_theater_logic.h"

struct TitleAdapterRuntimeSnapshot
{
    TitleRuntimeSnapshot runtime{};
    bool ownershipPending = false;
};

const TitleDescriptor* TitleAdapter_PollLoaded(uint64_t observedAtMs);
// The loader is unmapping the DLL at `base` (the loader's unload notification: LOADER LOCK HELD, atomic stores
// only). MCC loads a game DLL again the moment the old image is gone, and a reload gets the same base, so a
// poll can find the same base on both sides of an unload. The next poll publishes the gap it did not see, and
// the title starts a new generation in its new image.
void TitleAdapter_NoteImageUnloading(uintptr_t base) noexcept;
const TitleDescriptor* TitleAdapter_GetActive();
GameTitle TitleAdapter_GetActiveTitle();
uint64_t TitleAdapter_GetActiveTitleEpochMs();

uint32_t TitleAdapter_GetGeneration(GameTitle title);
TitleRuntimeAvailabilitySnapshot TitleAdapter_GetAvailability();
bool TitleAdapter_GetCandidate(
    GameTitle title, uint64_t heartbeatFreshForMs,
    TitleRuntimeCandidate& candidate);
TitleAdapterRuntimeSnapshot TitleAdapter_GetRuntimeSnapshot(
    uint64_t nowMs, const TitleRuntimeHeartbeatPolicy& policy,
    GameTitle retainedOwner = GameTitle::None);
bool TitleAdapter_PublishLifecycle(
    GameTitle title, uint32_t generation,
    const TitleRuntimeLifecycle& lifecycle);
bool TitleAdapter_PublishMode(
    GameTitle title, uint32_t generation, RuntimeMode mode);
bool TitleAdapter_PublishHeartbeat(
    GameTitle title, uint32_t generation, uint64_t heartbeatMs);
bool TitleAdapter_ClearHeartbeat(GameTitle title, uint32_t generation);

// Universal cutscene-camera contract. Title adapters publish only verified
// engine state; the shared compositor decides presentation without title-name
// checks. Publications are generation tagged and become Unknown when stale.
bool TitleAdapter_PublishCinematicControl(
    GameTitle title, uint32_t generation,
    CinematicControlState state, uint64_t heartbeatMs);
void TitleAdapter_ClearCinematicControl(
    GameTitle title, uint32_t generation);
CinematicControlPublication TitleAdapter_GetCinematicControlPublication(
    GameTitle title);
bool TitleAdapter_PublishCutsceneTheaterProjection(
    GameTitle title, uint32_t generation,
    float authoredAspect, uint64_t heartbeatMs);
void TitleAdapter_ClearCutsceneTheaterProjection(
    GameTitle title, uint32_t generation);
CutsceneTheaterProjectionPublication
TitleAdapter_GetCutsceneTheaterProjectionPublication(GameTitle title);

void TitleAdapter_SetRuntimeMode(RuntimeMode mode);
RuntimeMode TitleAdapter_GetRuntimeMode();
