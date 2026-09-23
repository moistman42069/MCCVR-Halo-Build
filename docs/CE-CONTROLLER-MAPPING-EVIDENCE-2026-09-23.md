# CE controller action mapping evidence

## Scope and pinned inputs

The intended behavior is the same semantic VR action in every title, regardless
of the selected MCC controller preset. This evidence supplies CE's current
action-to-controller transport lookup. It does not establish independent action
injection when two native actions share one controller button.

Discovery used the official CE editing kit first, then matched its constructs
against the pinned retail module. Neither executable was launched or modified.

| Input | SHA-256 |
| --- | --- |
| HCEEK `halo_tag_test.exe` | `FC9E2B6193C6F6D9FF988278B0D39A983747F3FDBDECA7CAFADD28F1F0C53E73` |
| Retail `halo1.dll` | `0A12DC561780F449D3F4D0DF10BB8D3BC7BE7840A5BEB2B236F672EB6CD42E6C` |

All addresses below are RVAs. The kit image base is `400000`; the retail image
base is `180000000`. Raw decompilations and independent instruction checks are
preserved under `out/coop-stability-20260923/ce-input-*`.

## Action identity

Kit `1BF8D0` resolves 33 control names through its pointer table `8117B0`.
Retail table `1887C20` contains the exact same 33 names in the same order.
`tools/re/verify_ce_controller_mapping.py` checks both tables directly.

| VR action | CE action number | Native name |
| --- | ---: | --- |
| Fire | 7 | `fire` |
| Grenade | 6 | `throw_grenade` |
| Jump | 0 | `jump` |
| Melee | 4 | `melee` |
| Reload | 13 | `reload` |
| Interact | 2 | `action` |
| Switch weapon | 3 | `switch_weapon` |
| Switch grenade | 1 | `switch_grenade` |
| Crouch | 10 | `crouch` |
| Zoom | 11 | `zoom` |
| Flashlight | 5 | `flashlight` |

CE has no separate armor-equipment or sprint action in this table. Do not
invent either action or silently map one to the flashlight.

## Current preference row

Kit `1BFCB0` initializes a controller's gamepad bindings at
`preferences + 12A + controller * 42`. There are 33 signed 16-bit entries.
Kit `1BF2D0` reads that same representation for binding display. Kit `1C23A0`,
the input-abstraction updater, selects the controller's `8A0`-byte preference
bank and consumes the corresponding controller row within it. Consequently the
effective stride between each controller's active row is **`8E2`, not `8A0`**.
Its source assertion identifies `-1` as `_xi_gamepad_button_unused` and accepts
physical button indices `0..15`.

Retail `ADD6FC` is the matched updater. `ADD772` loads preference base
`2EA0610`; `ADD7E4` multiplies controller by `8A0`; `ADE503` multiplies that
same controller by `21`; `ADE515` reads the signed-binding representation at
`selected_preferences + 12A + (controller * 21 + action) * 2`.

The read-only resolver therefore uses:

```text
address = module + 2EA0610 + controller * 8E2 + 12A + action * 2
controller in [0, 3], action in [0, 32]
entry = signed int16; -1 means unbound; only 0..15 is a button transport
```

The ordinary preference copier `ADC5A4` independently identifies the bank base
and `8A0` bank stride. Resolve the base from its RIP-relative operand, not an
unchecked absolute pointer. The displacement is at `ADC5A4+6`, and the
instruction's next address is `ADC5A4+10`.

Both patterns below occur exactly once in the pinned retail image:

```text
ADC5A4 preference reader / base load:
4C 8B CA 48 8D 15 ?? ?? ?? ?? 83 F9 04 7D 0D 48 63 C1
4C 69 C0 A0 08 00 00 49 03 D0 41 B8 A0 08 00 00 49 8B C9 E9 ?? ?? ?? ??

ADE503 controller-row / action consumer:
49 6B C7 21 44 8B EE 48 89 44 24 40 4D 63 FD 49 03 C7
45 0F B7 A4 46 2A 01 00 00 83 C8 FF 66 44 3B E0 0F 84 1B 03 00 00
```

CE has no controller-index field corresponding to the later engines'
`controllerOffset` check. Reading a guessed field would reject valid CE rows.
The worker must retain module pinning, exact signature admission, module-range
bounds, SEH protection, generation tagging and snapshot expiry. No pointer or
game-memory read belongs in the XInput hot hook.

## CE's own physical-button transport

Kit `1C6660`, called by the XInput-get-state adapter `1C63F0`, converts raw
XInput state into the 16-button native gamepad state. Source-path evidence is
`input/input_xinput.c`, reached from `1C6590`'s `XInputGetState` import setup.
Buttons 0 and 1 read raw state bytes 6 and 7 respectively: left and right trigger.
Buttons 2 through 15 use the following 14-dword table at kit `686CD8`:

```text
0001 0002 0004 0008 0010 0020 0040 0080
1000 2000 4000 8000 0100 0200
```

The exact table exists once in retail at `18A8AC0`. Retail converter `C0E330`
selects raw bytes 6/7 at `C0E37E..C0E384`; `C0E421` loads that mask table;
`C0E42C` masks raw word+4 and builds native held-frame/held-time records. This
independently proves CE's transport order, even though it happens to equal the
existing H3/ODST/Reach/H4 order. H2 uses a different table.

## Remaining action-separation boundary

This is a layout-aware transport adapter. Native `ADD6FC` still loops over
every action and reads the physical button named by that action's preference
entry. If Reload and Interact name the same physical button, emitting that
button supplies both native actions. A VR setting that leaves one action
unbound cannot isolate it by changing the XInput bit alone. Conversely,
suppressing that bit suppresses both actions. The kit's default initializer
already assigns the same button to `action` and `reload`; this is a concrete
native alias, not a theoretical concern.

Independent action injection requires its own native integration after device
mapping, preserving held-frame/time semantics and local-controller ownership.
It is not established by the read-only preference proof and must not be claimed
as complete on the strength of this resolver.

## Offline validation

`python tools/re/verify_ce_controller_mapping.py` checks pinned input hashes,
both 33-name tables, CE's own 14-mask tables, unique retail consumers, and the
resolved preference pointer. These are static native-contract checks. They do
not replace headset or controller-preset testing, and do not claim a co-op fix.
