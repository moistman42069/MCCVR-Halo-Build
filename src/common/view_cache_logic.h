#pragma once

#include <cstddef>
#include <cstdint>

// A fixed-capacity, allocation-free table mapping a render resource we did not
// create to a view we did.
//
// WHY THIS EXISTS. The shared eye/menu/screen upload path cached exactly ONE
// source view, keyed on the source texture pointer. A single frame publishes
// the left eye and then the right eye from two DIFFERENT textures, so that key
// never matched twice in a row: every eye released its predecessor's view and
// asked the device for a new one. Add the F1 menu, the desktop screen quad and
// the reticle - each publishing from its own texture - and a busy frame evicts
// the one slot four or five times. Every miss is a COM device call on the
// render thread, which AGENTS.md forbids in a hot hook outright. The cutscene
// theatre already worked around this locally by giving itself a per-eye keyed
// pair; this generalises that fix to the path every title shares.
//
// WHY A RAW POINTER IS A SOUND KEY. The view we hold keeps a strong reference
// on the resource, so a cached resource cannot be freed while it is in the
// table, and therefore its address cannot be recycled by a later allocation.
// A pointer match is an identity match for as long as the entry lives. The
// owner must still Forget() a resource it is deliberately recreating (a
// resolution change, a level load), which is what keeps this table from
// pinning a dead full-resolution texture alive.
//
// WHY LEAST-RECENTLY-USED. The steady-state working set is small and fixed -
// two eyes, menu, screen, reticle, scope - so in normal play the table fills
// once and never evicts again. A full table means the set genuinely changed,
// and LRU keeps the live eyes rather than whichever texture happened to arrive
// first.
//
// This header is deliberately free of Direct3D types so the whole policy can
// be exercised offline in core_tests.

struct ViewCacheLookup
{
    void* view = nullptr;  // The cached view on a hit; null on a miss.
    bool hit = false;
};

struct ViewCacheStats
{
    uint64_t hits = 0;
    uint64_t misses = 0;
    uint64_t evictions = 0;
};

template <std::size_t Capacity>
class ViewCacheTable
{
public:
    static_assert(Capacity > 0, "a zero-capacity cache would create every frame");

    static constexpr std::size_t kCapacity = Capacity;

    // Look `key` up. A hit marks the entry as most recently used, so the
    // resources a frame actually touches are the ones that survive eviction.
    ViewCacheLookup Find(const void* key)
    {
        if (!key)
            return {};
        for (std::size_t i = 0; i < Capacity; ++i)
        {
            if (m_entries[i].key == key && m_entries[i].view)
            {
                m_entries[i].tick = ++m_tick;
                ++m_stats.hits;
                return {m_entries[i].view, true};
            }
        }
        ++m_stats.misses;
        return {};
    }

    // Publish the view the caller just created for `key`. Returns a view the
    // caller MUST release - either the evicted least-recently-used entry, or a
    // previous view for this same key. Returns null when nothing was displaced.
    //
    // A null view is not stored: a failed creation must not poison the slot
    // into reporting a hit that hands back nothing.
    void* Insert(const void* key, void* view)
    {
        if (!key || !view)
            return view;

        std::size_t chosen = Capacity;
        void* displaced = nullptr;

        for (std::size_t i = 0; i < Capacity; ++i)
        {
            if (m_entries[i].key == key)
            {
                chosen = i;
                displaced = m_entries[i].view;
                break;
            }
        }
        if (chosen == Capacity)
        {
            for (std::size_t i = 0; i < Capacity; ++i)
            {
                if (!m_entries[i].view)
                {
                    chosen = i;
                    break;
                }
            }
        }
        if (chosen == Capacity)
        {
            std::size_t oldest = 0;
            for (std::size_t i = 1; i < Capacity; ++i)
            {
                if (m_entries[i].tick < m_entries[oldest].tick)
                    oldest = i;
            }
            chosen = oldest;
            displaced = m_entries[chosen].view;
            ++m_stats.evictions;
        }

        m_entries[chosen].key = key;
        m_entries[chosen].view = view;
        m_entries[chosen].tick = ++m_tick;
        return displaced;
    }

    // Drop one resource the owner is about to recreate or destroy. Returns the
    // view to release, or null if the key was not cached.
    void* Forget(const void* key)
    {
        if (!key)
            return nullptr;
        for (std::size_t i = 0; i < Capacity; ++i)
        {
            if (m_entries[i].key == key)
            {
                void* view = m_entries[i].view;
                m_entries[i] = {};
                return view;
            }
        }
        return nullptr;
    }

    // Teardown drain. Call in a loop until it returns null, releasing each
    // view. Keeping the drain caller-side means this header never needs to
    // know how a view is released.
    void* TakeAny()
    {
        for (std::size_t i = 0; i < Capacity; ++i)
        {
            if (m_entries[i].view)
            {
                void* view = m_entries[i].view;
                m_entries[i] = {};
                return view;
            }
        }
        return nullptr;
    }

    bool Contains(const void* key) const
    {
        if (!key)
            return false;
        for (std::size_t i = 0; i < Capacity; ++i)
        {
            if (m_entries[i].key == key && m_entries[i].view)
                return true;
        }
        return false;
    }

    std::size_t Size() const
    {
        std::size_t used = 0;
        for (std::size_t i = 0; i < Capacity; ++i)
        {
            if (m_entries[i].view)
                ++used;
        }
        return used;
    }

    const ViewCacheStats& Stats() const { return m_stats; }

    void ResetStats() { m_stats = {}; }

private:
    struct Entry
    {
        const void* key = nullptr;
        void* view = nullptr;
        uint64_t tick = 0;
    };

    Entry m_entries[Capacity]{};
    uint64_t m_tick = 0;
    ViewCacheStats m_stats{};
};

// A fixed-capacity pool of intermediate copies, keyed by SHAPE rather than by
// identity. The old code kept one intermediate and rebuilt it whenever the
// requested width, height or format differed from the one in hand. Two
// different-sized slow-path sources in the same frame - a full-resolution eye
// and a small reticle, say - therefore destroyed and recreated a
// FULL-RESOLUTION texture every single frame. vr.cpp's own comment calls that
// "exactly the kind of cost that halves the frame rate the moment a level
// loads"; keeping one entry per shape removes it.
//
// Slots are matched on the shape triple, so a pool of a handful of shapes
// covers every source the upload path sees. Eviction is least-recently-used
// for the same reason as above.

struct IntermediateShape
{
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t format = 0;

    bool Valid() const { return width != 0 && height != 0; }

    bool operator==(const IntermediateShape& other) const
    {
        return width == other.width && height == other.height &&
               format == other.format;
    }
    bool operator!=(const IntermediateShape& other) const
    {
        return !(*this == other);
    }
};

struct IntermediatePoolSlot
{
    std::size_t index = 0;
    bool valid = false;   // false when the shape was unusable
    bool needsCreate = false;
    IntermediateShape evicted{};  // shape the caller must release first
};

template <std::size_t Capacity>
class IntermediatePoolTable
{
public:
    static_assert(Capacity > 0, "a zero-capacity pool would create every frame");

    static constexpr std::size_t kCapacity = Capacity;

    // Reserve the slot that should hold `shape`. On an existing match the slot
    // is returned with needsCreate false and nothing is displaced. Otherwise
    // the caller must create the resource into `index`, releasing whatever
    // occupies it first when `evicted.Valid()`.
    IntermediatePoolSlot Acquire(const IntermediateShape& shape)
    {
        IntermediatePoolSlot result{};
        if (!shape.Valid())
            return result;
        result.valid = true;

        for (std::size_t i = 0; i < Capacity; ++i)
        {
            if (m_slots[i].live && m_slots[i].shape == shape)
            {
                m_slots[i].tick = ++m_tick;
                result.index = i;
                ++m_stats.hits;
                return result;
            }
        }
        ++m_stats.misses;
        result.needsCreate = true;

        for (std::size_t i = 0; i < Capacity; ++i)
        {
            if (!m_slots[i].live)
            {
                result.index = i;
                return result;
            }
        }

        std::size_t oldest = 0;
        for (std::size_t i = 1; i < Capacity; ++i)
        {
            if (m_slots[i].tick < m_slots[oldest].tick)
                oldest = i;
        }
        result.index = oldest;
        result.evicted = m_slots[oldest].shape;
        ++m_stats.evictions;
        return result;
    }

    // Confirm the caller created the resource for the slot Acquire() handed
    // back. A failed creation must call Abandon() instead, so a dead slot is
    // never reported as a match.
    void Commit(std::size_t index, const IntermediateShape& shape)
    {
        if (index >= Capacity || !shape.Valid())
            return;
        m_slots[index].shape = shape;
        m_slots[index].live = true;
        m_slots[index].tick = ++m_tick;
    }

    void Abandon(std::size_t index)
    {
        if (index >= Capacity)
            return;
        m_slots[index] = {};
    }

    bool Live(std::size_t index) const
    {
        return index < Capacity && m_slots[index].live;
    }

    IntermediateShape ShapeAt(std::size_t index) const
    {
        if (index >= Capacity)
            return {};
        return m_slots[index].shape;
    }

    std::size_t Size() const
    {
        std::size_t used = 0;
        for (std::size_t i = 0; i < Capacity; ++i)
        {
            if (m_slots[i].live)
                ++used;
        }
        return used;
    }

    void Clear()
    {
        for (std::size_t i = 0; i < Capacity; ++i)
            m_slots[i] = {};
    }

    const ViewCacheStats& Stats() const { return m_stats; }

    void ResetStats() { m_stats = {}; }

private:
    struct Slot
    {
        IntermediateShape shape{};
        bool live = false;
        uint64_t tick = 0;
    };

    Slot m_slots[Capacity]{};
    uint64_t m_tick = 0;
    ViewCacheStats m_stats{};
};
