#pragma once
#include <cstddef>
#include <cstdint>
bool HaloCENetworkInput_Poll(uintptr_t base,size_t size,uint32_t generation,bool active) noexcept;
// Network actions must not subsequently be overwritten by local-only body or
// movement adapters. False for local campaigns, films and unknown bindings.
bool HaloCENetworkInput_UsesNativeSimulation() noexcept;
