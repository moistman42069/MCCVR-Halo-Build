#pragma once

#include <cstdint>

enum class Halo4AdapterStage : uint8_t
{
    Disabled = 0,
    ControllerInputOnly,
    // C-H4-2: ControllerInputOnly plus the level-load gate and the one-shot
    // loaded-image identity/anchor preflight. Still no hook of any kind.
    ControllerInputAndColdObservation,
};

struct Halo4EvidenceIdentity
{
    const wchar_t* moduleName;
    const char* moduleSha256Steam;
    const char* moduleSha256Store;
    uint32_t peTimestamp;
    uint32_t sizeOfImage;
    const char* h4ekBuild;
};

struct Halo4HookProof
{
    bool retailIdentity;
    uint32_t loadedImageMatchCount;
    bool executableRange;
    bool abi;
    bool callers;
    bool dataFlow;
    bool h4ekSemantics;
    bool consumedLayoutFields;
};

Halo4AdapterStage Halo4Adapter_GetStage();
const Halo4EvidenceIdentity& Halo4Adapter_GetEvidenceIdentity();
bool Halo4Adapter_HookProofComplete(const Halo4HookProof& proof);
bool Halo4Adapter_RuntimeHooksPermitted();
