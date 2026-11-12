#pragma once
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

// FORWARDING FROM A C++ DETOUR TO A TITLE NATIVE: call the trampoline through CallNativeWithSpareSlots.
//
// The titles are LTCG builds. When every native caller of a function reserves stack past its declared
// arguments, LTCG may let the function use that stack as scratch. ODST's damage event 3B68A8 takes six
// arguments and keeps a bool at entry+0x38, the slot a seventh would use. A C++ detour that forwards exactly
// the declared arguments reserves exactly those, so the native writes into the detour's own frame: in ODST that
// byte lands on the detour's saved r15, which goes back to the native caller one byte off and crashes a
// different function, the unit-action updater (docs/ALL-TITLES-AIM-2026-09-29.md, "FIXED: the ODST
// crashes"). Nothing at build or review time shows it: the crash is in another function, much later.
//
// CallNativeWithSpareSlots calls `original` with the declared arguments followed by zeroed spare slots: enough
// to fill the four register arguments, then kNativeSpareStackSlots more on the stack. The stack the callee may
// touch past its arguments, up to 0x20 bytes, then belongs to this call. The spares are volatile argument
// registers and caller-cleaned stack, so a callee that ignores them is unaffected.
//
// What it does NOT cover: a native that reaches further than that, or that relies on a volatile register its
// callers keep live across the call (Reach's render gate 0x6527C, CE's bone convert halo1+0x77E70 -- those need
// an asm thunk). tools/audit_caller_area.py measures both for a hooked function.
inline constexpr size_t kNativeSpareStackSlots = 4;

namespace native_forward_detail
{
template <size_t>
using SpareSlot = uint64_t;

template <typename Fn, typename Spares>
struct Padded;

template <typename Ret, typename... Params, size_t... I>
struct Padded<Ret (*)(Params...), std::index_sequence<I...>>
{
    using Type = Ret (*)(Params..., SpareSlot<I>...);

    static Ret Call(Type fn, Params... args) { return fn(args..., SpareSlot<I>{0}...); }
};

template <size_t Declared>
using Spares = std::make_index_sequence<(Declared < 4 ? 4 - Declared : 0) + kNativeSpareStackSlots>;
} // namespace native_forward_detail

template <typename Ret, typename... Params>
Ret CallNativeWithSpareSlots(Ret (*original)(Params...), std::type_identity_t<Params>... args)
{
    using Padded = native_forward_detail::Padded<Ret (*)(Params...),
                                                 native_forward_detail::Spares<sizeof...(Params)>>;
    return Padded::Call(reinterpret_cast<typename Padded::Type>(original), args...);
}
