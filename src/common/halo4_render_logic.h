#pragma once

#include <cstddef>
#include <cstdint>

// Halo 4-only render evidence and (eventually) allocation-free policy. This
// file contains no Windows, COM, MinHook, logging, or engine writes, so its
// contents can be exhaustively tested offline before any detour is
// authorized. It currently holds pinned identity constants ONLY: no RVA,
// AOB, stride, or layout may be added here without its proof first recorded
// in docs/HALO4-SIGNATURE-EVIDENCE.md (H4EK-first; the Reach script-table
// retail-derivation chain is UNPROVEN for Halo 4).

inline constexpr size_t kHalo4RetailFileSize = 17829336;
inline constexpr size_t kHalo4RetailImageSize = 0x04A3F000;
inline constexpr uint32_t kHalo4RetailPeTimestamp = 0x68A0E7BF;

// One retail Halo 4 build, signed once per storefront. As documented for the
// other titles (docs/MCC-EDITIONS-EVIDENCE.md), the Steam and Microsoft
// Store images are byte-identical apart from the Authenticode certificate,
// so the whole-file digest differs per edition while every RVA, the PE
// timestamp, and the image size stay shared. Both digests describe the one
// code image this file pins; an MCC update invalidates the whole table
// loudly through the identity preflight.
inline constexpr const char* kHalo4RetailModuleSha256[] = {
    // Steam
    "7C53E7D5BC9848545A1B70E2768242479336FBA1B7630D7AB955F7FD0C34FA84",
    // Microsoft Store / Xbox app (Game Pass)
    "5767CD564C1E8E8D012D002A8DE8E92960A3DE46442399ED054E3C4EF44AA496",
};

// Kit-vs-retail build drift, same shape Reach had: kit facts transfer as
// semantics and layouts only, never as addresses.
inline constexpr char kHalo4KitBuildTag[] = "2023.06.27.176405.1-Release";
inline constexpr char kHalo4RetailBuildTag[] = "2025.08.16.178512.1";
