// Included by game.cpp and the production native seat lifetime fixture.
    enum class Halo3NativeSeatState : uint32_t
    {
        Stock = 0,
        Active,
        StockFallback,
    };

    // C17 loaded-tag transaction. Camera-thread owned; worker-visible fields
    // are atomics used only for transition logging. The saved values are
    // restored on seat change/exit or when the feature is disabled.
    struct Halo3NativeSeatPatch
    {
        uint32_t generation = 0;
        uint32_t definitionIndex = 0xFFFFFFFFu;
        int seatIndex = -1;
        uint32_t* flags = nullptr;
        unsigned char* tagBase = nullptr;
        unsigned char* definition = nullptr;
        void* instances = nullptr;
        int32_t* cameraTrackCount = nullptr;
        uint32_t originalFlags = 0;
        int32_t originalCameraTrackCount = 0;
        bool active = false;
    };
    Halo3NativeSeatPatch g_halo3NativeSeatPatch;
    std::atomic<uint32_t> g_halo3NativeSeatState{
        static_cast<uint32_t>(Halo3NativeSeatState::Stock)};
    std::atomic<uint32_t> g_halo3NativeSeatSerial{0};

    // A module generation does not identify a loaded map's tag allocation.
    // Re-resolve from current storage before reading or restoring the lease.
    uint32_t* Halo3ResolveOwnedSeatStorage(const Halo3NativeSeatPatch& patch)
    {
        if (!patch.active || !patch.flags || !patch.generation ||
            patch.generation != TitleAdapter_GetGeneration(GameTitle::Halo3))
            return nullptr;
        auto* tagBase = g_halo3TagDataBase
            ? static_cast<unsigned char*>(*g_halo3TagDataBase) : nullptr;
        void* instances = g_halo3TagInstanceTable ? *g_halo3TagInstanceTable : nullptr;
        if (!tagBase || tagBase != patch.tagBase || !instances || instances != patch.instances)
            return nullptr;
        auto* definition = Halo3LoadedTagDefinition(patch.definitionIndex);
        if (!definition || definition != patch.definition) return nullptr;
        const int32_t count = *reinterpret_cast<const int32_t*>(
            definition + kHalo3VehicleSeatsBlockOffset);
        const uint32_t address = *reinterpret_cast<const uint32_t*>(
            definition + kHalo3VehicleSeatsBlockOffset + 4);
        if (!address || count <= 0 || count > 126 ||
            patch.seatIndex < 0 || patch.seatIndex >= count) return nullptr;
        auto* liveFlags = reinterpret_cast<uint32_t*>(tagBase + size_t(address)*4 +
            size_t(patch.seatIndex)*kHalo3VehicleSeatStride);
        return liveFlags == patch.flags ? liveFlags : nullptr;
    }

    bool Halo3NativeSeatStillOwned(const Halo3NativeSeatPatch& patch)
    {
        __try
        {
            const auto* flags = Halo3ResolveOwnedSeatStorage(patch);
            return flags && *flags == Halo3FirstPersonSeatFlags(patch.originalFlags);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    // The occupied Halo 3 seat's own flags, or 0 when no seat is owned. The
    // first-person seat patch already re-reads this record from the loaded tag
    // on entry and keeps the ORIGINAL word, so no second tag walk is needed.
    uint32_t Halo3OccupiedSeatFlags()
    {
        const Halo3NativeSeatPatch& patch = g_halo3NativeSeatPatch;
        if (!patch.active ||
            g_halo3NativeSeatState.load(std::memory_order_acquire) !=
                static_cast<uint32_t>(Halo3NativeSeatState::Active))
        {
            return 0;
        }
        return patch.originalFlags;
    }

    void Halo3RestoreNativeSeatPatch()
    {
        Halo3NativeSeatPatch& patch = g_halo3NativeSeatPatch;
        const bool hadState = patch.active ||
            g_halo3NativeSeatState.load(std::memory_order_relaxed) !=
                static_cast<uint32_t>(Halo3NativeSeatState::Stock);
        if (patch.active && patch.generation &&
            patch.generation == TitleAdapter_GetGeneration(GameTitle::Halo3))
        {
            __try
            {
                const uint32_t patchedFlags =
                    Halo3FirstPersonSeatFlags(patch.originalFlags);
                // Storage identity and compare/exchange are both required:
                // a reused address or concurrent native writer is not ours.
                auto* liveFlags = Halo3ResolveOwnedSeatStorage(patch);
                if (liveFlags)
                    InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(liveFlags),
                        static_cast<LONG>(patch.originalFlags), static_cast<LONG>(patchedFlags));
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                // The loaded map may already be disappearing. Feature cleanup
                // must never disarm the working camera core.
            }
        }
        patch = {};
        g_halo3NativeAnchorCalibration = {};
        g_halo3NativeSeatState.store(
            static_cast<uint32_t>(Halo3NativeSeatState::Stock),
            std::memory_order_release);
        if (hadState)
            g_halo3NativeSeatSerial.fetch_add(1, std::memory_order_release);
    }

    // C20: clear the occupied seat's `third person camera` bit for as long as
    // the VR vehicle camera owns the view, so Halo puts the occupant in its own
    // first-person state and stops drawing the player's character model in the
    // space the headset is looking out of. Halo re-reads the bit from the
    // loaded tag every camera frame (halo3+0x1326E8 -> +0x212198), so the
    // effect is immediate and the restore below is complete.
    //
    // This is NOT C17. C17 additionally zeroed the seat's camera-track count
    // and then made the resulting native camera the motion parent; that was
    // headset-rejected. Only the flag is written here, the camera-track count
    // is read as a bounds check and never modified, and the camera itself
    // remains the accepted C18/C19 authored-node anchor.
    bool Halo3EnsureFirstPersonSeatFlag(uint32_t definitionIndex, int seatIndex,
                                        uint32_t generation)
    {
        Halo3NativeSeatPatch& patch = g_halo3NativeSeatPatch;
        const bool sameSeat = patch.generation == generation &&
            patch.definitionIndex == definitionIndex &&
            patch.seatIndex == seatIndex;
        if (!g_config.vehicle_first_person || !g_config.vehicle_hide_body)
        {
            Halo3RestoreNativeSeatPatch();
            return false;
        }
        if (sameSeat && patch.active && Halo3NativeSeatStillOwned(patch))
            return true;
        if (sameSeat && g_halo3NativeSeatState.load(
                std::memory_order_relaxed) ==
                static_cast<uint32_t>(Halo3NativeSeatState::StockFallback))
            return false;

        Halo3RestoreNativeSeatPatch();
        patch.generation = generation;
        patch.definitionIndex = definitionIndex;
        patch.seatIndex = seatIndex;
        bool installed = false;
        __try
        {
            void** instSlot = g_halo3TagInstanceTable;
            void** baseSlot = g_halo3TagDataBase;
            auto* instTable = instSlot
                ? static_cast<unsigned char*>(*instSlot) : nullptr;
            auto* tagBase = baseSlot
                ? static_cast<unsigned char*>(*baseSlot) : nullptr;
            if (instTable && tagBase && definitionIndex <= 0xFFFFu &&
                seatIndex >= 0 && seatIndex <= kHalo3VehicleSeatMax)
            {
                const uint32_t instanceDword =
                    *reinterpret_cast<const uint32_t*>(
                        instTable + static_cast<size_t>(definitionIndex) * 8 + 4);
                auto* definition = instanceDword
                    ? tagBase + static_cast<size_t>(instanceDword) * 4 : nullptr;
                const int32_t seatCount = definition
                    ? *reinterpret_cast<const int32_t*>(
                          definition + kHalo3VehicleSeatsBlockOffset)
                    : 0;
                const uint32_t seatsAddress = definition
                    ? *reinterpret_cast<const uint32_t*>(
                          definition + kHalo3VehicleSeatsBlockOffset + 4)
                    : 0;
                if (definition && seatCount > 0 && seatCount <= 126 &&
                    seatIndex < seatCount && seatsAddress)
                {
                    auto* seat = tagBase +
                        static_cast<size_t>(seatsAddress) * 4 +
                        static_cast<size_t>(seatIndex) * kHalo3VehicleSeatStride;
                    auto* flags = reinterpret_cast<uint32_t*>(seat);
                    auto* trackCount = reinterpret_cast<int32_t*>(
                        seat + kHalo3SeatCameraTracksBlockOffset);
                    const uint32_t originalFlags = *flags;
                    const int32_t originalTrackCount = *trackCount;
                    // Every official H3 player seat has bit 4. Requiring it is
                    // the live layout proof that keeps a bad tag walk stock.
                    // The track count is only a second bounds check on the same
                    // seat record; the reference mod keeps those tracks and so
                    // do we.
                    if (Halo3SeatFlagsLookLikePlayerSeat(originalFlags) &&
                        originalTrackCount >= 0 && originalTrackCount <= 16)
                    {
                        const uint32_t patchedFlags =
                            Halo3FirstPersonSeatFlags(originalFlags);
                        patch.flags = flags;
                        patch.tagBase = tagBase;
                        patch.definition = definition;
                        patch.instances = instTable;
                        patch.cameraTrackCount = trackCount;
                        patch.originalFlags = originalFlags;
                        patch.originalCameraTrackCount = originalTrackCount;
                        installed = static_cast<uint32_t>(InterlockedCompareExchange(
                            reinterpret_cast<volatile LONG*>(flags),
                            static_cast<LONG>(patchedFlags), static_cast<LONG>(originalFlags))) == originalFlags;
                        patch.active = installed;
                    }
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            installed = false;
        }
        if (!installed)
        {
            Halo3RestoreNativeSeatPatch();
            patch.generation = generation;
            patch.definitionIndex = definitionIndex;
            patch.seatIndex = seatIndex;
            g_halo3NativeSeatState.store(
                static_cast<uint32_t>(Halo3NativeSeatState::StockFallback),
                std::memory_order_release);
            g_halo3NativeSeatSerial.fetch_add(1, std::memory_order_release);
            return false;
        }
        g_halo3NativeAnchorCalibration = {};
        g_halo3NativeSeatState.store(
            static_cast<uint32_t>(Halo3NativeSeatState::Active),
            std::memory_order_release);
        g_halo3NativeSeatSerial.fetch_add(1, std::memory_order_release);
        return true;
    }

