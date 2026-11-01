// Included by game.cpp and the native seat lifetime fixture.
    // The same three states as Halo 3's Halo3NativeSeatState. Active is the only one in which the seat is held
    // in the game's first-person camera (C20); StockFallback is a seat this patch refused or failed to take.
    enum class OdstNativeSeatState : uint32_t
    {
        Stock = 0,
        Active,
        StockFallback,
    };

    // Seat-flag patch (hide body). Same transaction shape as Halo 3's: patch
    // only the occupied loaded seat, restore only a value that is still ours.
    struct OdstNativeSeatPatch
    {
        uint32_t generation = 0;
        uint32_t definitionIndex = 0xFFFFFFFFu;
        int seatIndex = -1;
        uint32_t* flags = nullptr;
        unsigned char* tagBase = nullptr;
        unsigned char* definition = nullptr;
        void* instances = nullptr;
        uint32_t originalFlags = 0;
        bool active = false;
    };
    OdstNativeSeatPatch g_odstNativeSeatPatch;
    std::atomic<uint32_t> g_odstNativeSeatState{
        static_cast<uint32_t>(OdstNativeSeatState::Stock)};
    std::atomic<uint32_t> g_odstNativeSeatSerial{0};
    // The occupied seat's ORIGINAL tag flags lack bit 4 (third-person camera): C20 refused it, and unmodded ODST
    // rebases this seat's aim by the vehicle's up frame itself. Same contract as Halo 3's
    // g_halo3SeatNativeNoThirdPerson (seat_aim_world_frame.inl stands down and logs the seat once).
    std::atomic<uint32_t> g_odstSeatNativeNoThirdPerson{0};

    constexpr uint32_t OdstSeatNativeNoThirdPersonKey(uint32_t definitionIndex, int seatIndex)
    {
        return 0x80000000u | (definitionIndex & 0xFFFFu) << 8 | (static_cast<uint32_t>(seatIndex) & 0xFFu);
    }

    bool OdstNativeSeatStillOwned(const OdstNativeSeatPatch& patch)
    {
        if (!patch.active || !patch.flags || !patch.generation ||
            patch.generation != TitleAdapter_GetGeneration(GameTitle::Halo3ODST)) return false;
        __try
        {
            auto* tagBase = g_odstTagDataBase
                ? static_cast<unsigned char*>(*g_odstTagDataBase) : nullptr;
            void* instances = g_odstTagInstanceTable ? *g_odstTagInstanceTable : nullptr;
            if (!tagBase || tagBase != patch.tagBase || !instances || instances != patch.instances)
                return false;
            auto* definition = OdstLoadedTagDefinition(patch.definitionIndex);
            if (!definition || definition != patch.definition) return false;
            const int32_t count = *reinterpret_cast<const int32_t*>(
                definition + kOdstVehicleSeatsCountOffset);
            const uint32_t address = *reinterpret_cast<const uint32_t*>(
                definition + kOdstVehicleSeatsDataOffset);
            if (!address || count <= 0 || count > 126 ||
                patch.seatIndex < 0 || patch.seatIndex >= count) return false;
            const auto* flags = reinterpret_cast<const uint32_t*>(tagBase + size_t(address)*4 +
                size_t(patch.seatIndex)*kOdstVehicleSeatStride + kOdstSeatFlagsOffset);
            return flags == patch.flags &&
                *flags == (patch.originalFlags & ~kOdstSeatThirdPersonCameraBit);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    void OdstRestoreNativeSeatPatch()
    {
        OdstNativeSeatPatch& patch = g_odstNativeSeatPatch;
        const bool hadState =
            g_odstNativeSeatState.load(std::memory_order_relaxed) !=
                static_cast<uint32_t>(OdstNativeSeatState::Stock);
        if (patch.active && patch.flags && patch.generation &&
            patch.generation == TitleAdapter_GetGeneration(GameTitle::Halo3ODST))
        {
            const uint32_t patchedFlags =
                patch.originalFlags & ~kOdstSeatThirdPersonCameraBit;
            __try
            {
                // Map/checkpoint transitions may retire or replace tag storage
                // before cleanup. Never dereference the saved pointer until
                // the current title's own tag walk resolves that same storage.
                auto* tagBase = g_odstTagDataBase
                    ? static_cast<unsigned char*>(*g_odstTagDataBase) : nullptr;
                void* instances = g_odstTagInstanceTable ? *g_odstTagInstanceTable : nullptr;
                if (!tagBase || tagBase != patch.tagBase || !instances || instances != patch.instances)
                    __leave;
                auto* definition = OdstLoadedTagDefinition(patch.definitionIndex);
                if (!definition || definition != patch.definition)
                    __leave;
                const int32_t count = *reinterpret_cast<const int32_t*>(
                    definition + kOdstVehicleSeatsCountOffset);
                const uint32_t address = *reinterpret_cast<const uint32_t*>(
                    definition + kOdstVehicleSeatsDataOffset);
                if (!address || count <= 0 || count > 126 ||
                    patch.seatIndex < 0 || patch.seatIndex >= count)
                    __leave;
                auto* liveFlags = reinterpret_cast<uint32_t*>(tagBase + size_t(address)*4 +
                    size_t(patch.seatIndex)*kOdstVehicleSeatStride + kOdstSeatFlagsOffset);
                if (liveFlags != patch.flags) __leave;
                // A concurrent engine/tag writer wins atomically.
                InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(liveFlags),
                    static_cast<LONG>(patch.originalFlags), static_cast<LONG>(patchedFlags));
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
        }
        patch = {};
        g_odstSeatNativeNoThirdPerson.store(0, std::memory_order_relaxed);
        if (hadState)
        {
            g_odstNativeSeatState.store(
                static_cast<uint32_t>(OdstNativeSeatState::Stock), std::memory_order_relaxed);
            g_odstNativeSeatSerial.fetch_add(1, std::memory_order_release);
        }
    }

    bool OdstEnsureFirstPersonSeatFlag(uint32_t definitionIndex, int seatIndex,
                                       uint32_t generation)
    {
        // Co-op safe: the seat's flags are data the simulation reads too (the firing assist, target acquisition, a
        // seated-eye function), so clearing the camera bit changes the game on this machine only and a shared
        // session ends at the first shot from the seat. While one-machine changes are not allowed the seat is left
        // as the game built it and keeps the game's own camera.
        if (!Sight_FireTimeChangesAllowed())
        {
            static uint32_t s_loggedGeneration = 0;
            if (s_loggedGeneration != generation)
            {
                s_loggedGeneration = generation;
                LOG("ODST first-person seat: not taken while co-op safe shots is on. The seat's data would have to "
                    "be edited on this machine only, which ends a shared session at the first shot from the seat; "
                    "seats keep the game's own camera");
            }
            OdstRestoreNativeSeatPatch();
            return false;
        }
        if (!g_config.vehicle_first_person || !g_config.vehicle_hide_body)
        {
            OdstRestoreNativeSeatPatch();
            return false;
        }
        OdstNativeSeatPatch& patch = g_odstNativeSeatPatch;
        const uint32_t state =
            g_odstNativeSeatState.load(std::memory_order_relaxed);
        const bool sameSeat = patch.generation == generation &&
            patch.definitionIndex == definitionIndex &&
            patch.seatIndex == seatIndex;
        if (sameSeat && state == static_cast<uint32_t>(OdstNativeSeatState::Active) &&
            OdstNativeSeatStillOwned(patch))
            return true;
        if (sameSeat && state == static_cast<uint32_t>(OdstNativeSeatState::StockFallback))
            return false;           // proven bad for this seat: never retry
        OdstRestoreNativeSeatPatch();
        patch.generation = generation;
        patch.definitionIndex = definitionIndex;
        patch.seatIndex = seatIndex;

        bool installed = false;
        bool nativeNoThirdPerson = false;
        if (definitionIndex <= 0xFFFFu && seatIndex >= 0 && seatIndex <= 125)
        {
            __try
            {
                unsigned char* definition =
                    OdstLoadedTagDefinition(definitionIndex);
                void** baseSlot = g_odstTagDataBase;
                auto* tagBase = baseSlot
                    ? static_cast<unsigned char*>(*baseSlot) : nullptr;
                if (definition && tagBase)
                {
                    const int32_t seatCount =
                        *reinterpret_cast<const int32_t*>(
                            definition + kOdstVehicleSeatsCountOffset);
                    const uint32_t seatsAddress =
                        *reinterpret_cast<const uint32_t*>(
                            definition + kOdstVehicleSeatsDataOffset);
                    if (seatCount > 0 && seatCount <= 126 &&
                        seatIndex < seatCount && seatsAddress)
                    {
                        unsigned char* seat = tagBase +
                            static_cast<size_t>(seatsAddress) * 4 +
                            static_cast<size_t>(seatIndex) *
                                kOdstVehicleSeatStride;
                        auto* flags = reinterpret_cast<uint32_t*>(
                            seat + kOdstSeatFlagsOffset);
                        const uint32_t originalFlags = *flags;
                        nativeNoThirdPerson =
                            (originalFlags & kOdstSeatThirdPersonCameraBit) == 0;
                        // Only ever touch a seat that actually is a
                        // third-person player seat today.
                        if (originalFlags & kOdstSeatThirdPersonCameraBit)
                        {
                            const uint32_t patchedFlags =
                                originalFlags & ~kOdstSeatThirdPersonCameraBit;
                            patch.flags = flags;
                            patch.tagBase = tagBase;
                            patch.definition = definition;
                            patch.instances = g_odstTagInstanceTable ? *g_odstTagInstanceTable : nullptr;
                            patch.originalFlags = originalFlags;
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
        }
        if (!installed)
        {
            OdstRestoreNativeSeatPatch();
            patch.generation = generation;
            patch.definitionIndex = definitionIndex;
            patch.seatIndex = seatIndex;
            g_odstSeatNativeNoThirdPerson.store(nativeNoThirdPerson
                ? OdstSeatNativeNoThirdPersonKey(definitionIndex, seatIndex) : 0u,
                std::memory_order_relaxed);
            g_odstNativeSeatState.store(
                static_cast<uint32_t>(OdstNativeSeatState::StockFallback), std::memory_order_relaxed);
            g_odstNativeSeatSerial.fetch_add(1, std::memory_order_release);
            return false;
        }
        g_odstNativeSeatState.store(
            static_cast<uint32_t>(OdstNativeSeatState::Active), std::memory_order_relaxed);
        g_odstNativeSeatSerial.fetch_add(1, std::memory_order_release);
        return true;
    }
