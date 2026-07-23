# LED Orchestra Matter Cluster

This is the first custom-cluster contract for the ESP-Matter/Thread prototype.
It mirrors the current shared effect model while leaving transport/security to
Matter.

## Identity

- Cluster name: `LedOrchestra`
- Cluster type: vendor custom cluster
- Prototype cluster id: `0xFFF1FC00`
- Endpoint: one LED segment endpoint per physical node for the first prototype
- Effect ids:
  - `0`: off
  - `1`: solid
  - `2`: rainbow
  - `3`: fibonacci — per-pixel R/G/B are consecutive Fibonacci numbers (mod 256)
    along the strip; scrolls down the strip with `speed` (`speed = 0` holds a
    static gradient). Ignores the `red`/`green`/`blue` fields.
  - `4`: aurora breathe — soft overlapping RGB waves with a breathing intensity
    curve; scrolls with `speed` and ignores the `red`/`green`/`blue` fields.
  - `5`: comet — single-color comet with a smooth fading tail; scrolls with
    `speed` and uses `red`/`green`/`blue`.
  - `6`: theater chase — three-phase single-color chase pattern; scrolls with
    `speed` and uses `red`/`green`/`blue`.
  - `7`: palette cycle — aurora palette gradient across the virtual strip;
    scrolls with `speed` and ignores the `red`/`green`/`blue` fields.
  - `8`: twinkle — deterministic sparse single-color twinkles keyed by global
    LED index and synchronized time; uses `red`/`green`/`blue`.
  - `9`: ocean drift — ambient layered underwater drift sampled from a palette
    (default `ocean`); slow and band-free. Palette-driven; honors the per-node
    palette override.
  - `10`: color wave — a luminous palette crest that travels the whole virtual
    strip (default `aurora`); built to be aligned hole-to-hole with the per-node
    calibration time offset so it reads as one continuous wave across physical
    gaps/bends. Palette-driven.
  - `11`: pulse reveal — a bright palette pulse sweeping over a dim wash (default
    `coral`); an attraction "look here now" cue. Palette-driven.
  - `12`: celebration — celebratory gold sparkle + shimmer (default `gold-score`)
    for a scoring moment. Palette-driven.
  - `13`: identify — install/diagnostics: render this node's segment in a color
    keyed to its orchestra node id with white-tipped segment ends, slow blink, so
    an installer can map which physical strip is which node id. Ignores RGB.
  - `14`: tidal surge — layered tide motion with bright whitecaps and deep
    undertow (default `seafoam-surf`) for ocean-theme perimeter runs.
    Palette-driven.
  - `15`: party confetti — dense neon confetti bursts over a dim wash (default
    `neon-party`) for birthday parties and full-venue party scenes.
    Palette-driven.
  - `16`: champagne toast — soft champagne shimmer with elegant glints (default
    `champagne-rose`) for anniversaries and other upscale occasion scenes.
    Palette-driven.

Palette-driven effects use the effect's default palette unless a per-node palette
override is set via `SetCalibration` (below). Effect ids and palette refs are an
append-only namespace — never reorder or reuse.

Use the private development VID/PID during prototypes. Do not allocate final
cluster ids or certification metadata until the Matter product direction is
locked.

## Commands

### SetScene

Fields:

| Tag | Type | Field |
| --- | --- | --- |
| `0` | `U8` | `effect_id` |
| `1` | `U8` | `red` |
| `2` | `U8` | `green` |
| `3` | `U8` | `blue` |
| `4` | `U8` | `speed` |
| `5` | `U8` | `brightness` |
| `6` | `U32` | `sequence` |
| `7` | `U64` | `scheduled_start_time_ms` |

Behavior:

- Reject unknown `effect_id` values.
- Store the accepted `sequence` in `last_sequence`. Stale-sequence rejection is
  a later refinement.
- Apply immediately when `scheduled_start_time_ms` is `0`.
- Otherwise apply at the synchronized local time matching the scheduled start.

### SetNodeConfig

Fields:

| Tag | Type | Field |
| --- | --- | --- |
| `0` | `U16` | `node_id` |
| `1` | `U16` | `segment_start` |
| `2` | `U16` | `segment_len` |
| `3` | `U16` | `total_leds` |
| `4` | `U8` | `led_gpio` |

Behavior:

- The node **persists** accepted config in NVS (magic + version + CRC wrapped)
  and reloads it at boot before the renderer/attributes rely on it. The boot log
  records the source: `config loaded from NVS ...` vs `... from defaults ...`. A
  corrupt/incompatible/missing record falls back to the compiled Kconfig defaults
  (it never blanks node identity). The storage record is versioned and
  append-only: bump the version and add a migration branch rather than reordering
  fields.
- A persistence failure does not fail the command — the live render state is
  already updated (keep-last-valid) and the hub can re-provision next boot.
- A runtime `led_gpio` change is still rejected: the strip driver is bound at
  boot, so a GPIO move needs a reboot/re-init.
- Use unicast, not a group command, for provisioning.

### SyncClock

Fields:

| Tag | Type | Field |
| --- | --- | --- |
| `0` | `U64` | `controller_time_ms` |

Behavior:

- Update the node's controller-time offset.
- Use the offset for synchronized effects and scheduled scenes.

### SetCalibration

Per-node **field tuning delivered as data, not firmware** — the knobs an
installer adjusts after the strips are on the wall (timing alignment, brightness
ceiling, palette choice, LED color correction) with no reflash. New effect
*behavior* still ships only via OTA; this command never carries code.

Fields (all optional — an omitted tag keeps the node's current value, so the
controller can tweak one knob in isolation):

| Tag | Type | Field |
| --- | --- | --- |
| `0` | `U32` | `time_offset_ms` (signed int32 carried as two's-complement bits) |
| `1` | `U8` | `brightness_cap` (0..255; 255 = no cap) |
| `2` | `U8` | `palette_override` (palette ref; `255` = use the effect default) |
| `3` | `U32` | `color_correction` (`0x00RRGGBB`; `0x00FFFFFF` = identity) |
| `4` | `U32` | `color_temperature` (`0x00RRGGBB`; `0x00FFFFFF` = identity) |

Behavior:

- `time_offset_ms` is added to **this node's render clock** for effect math only,
  so a synchronized wave/comet can be nudged hole-to-hole to look continuous
  across physical gaps and bends. It does **not** shift scheduled-scene
  activation — that always uses the unmodified synchronized clock — so a positive
  offset re-times the animation without desyncing when scenes flip.
- `brightness_cap` is the master-brightness ceiling applied after the per-scene
  `brightness` (the engine output policy's `master_brightness`).
- `palette_override` replaces the effect's default palette for palette-driven
  effects; `255` (`0xFF`) means "use the effect default". Non-palette effects
  ignore it. An unknown palette ref is rejected and not persisted.
- `color_correction` / `color_temperature` are per-channel LED correction /
  white-point trim for matching different strip batches or zones.
- The node **persists** accepted calibration in NVS (magic + version + CRC
  wrapped, append-only record) and reloads it at boot. Identity defaults
  (`time_offset_ms=0`, `brightness_cap=255`, `palette_override=255`, corrections
  `0x00FFFFFF`) mean an un-calibrated node renders exactly as before. A
  persistence failure does not fail the command (keep-last-valid).
- Use unicast for per-hole timing offsets; a group command is fine for a uniform
  zone brightness/palette.

## Prototype Attributes

| Id | Type | Attribute |
| --- | --- | --- |
| `0` | `U8` | `current_scene` |
| `1` | `U16` | `segment_start` |
| `2` | `U16` | `segment_len` |
| `3` | `U16` | `total_leds` |
| `4` | `U8` | `led_gpio` |
| `5` | `STR` | `firmware_version` |
| `6` | `U32` | `last_sequence` |
| `7` | `U8` | `calib_brightness_cap` |
| `8` | `U8` | `calib_palette_override` |
| `9` | `U32` | `calib_time_offset_ms` (signed int32 as two's-complement bits) |

The `calib_*` attributes mirror the last accepted `SetCalibration` so
`lo-read-config` can confirm per-node tuning survived a power-cycle without a
serial monitor.

## Controller Console Helpers

The controller-node registers these USB shell commands (unicast):

```text
lo-set-scene <node-id> <endpoint-id> <effect-id> <rrggbb> <speed> <brightness> [sequence] [scheduled-start-ms]
lo-set-node-config <node-id> <endpoint-id> <orchestra-node-id> <segment-start> <segment-len> <total-leds> <led-gpio>
lo-sync-clock <node-id> <endpoint-id> [controller-time-ms]
lo-set-calibration <node-id> <endpoint-id> <time-offset-ms|-> <brightness-cap|-> <palette-override|-> [corr-rrggbb|-] [temp-rrggbb|-]
```

and these group commands (see the [Group Commands](#group-commands) contract):

```text
lo-add-group <node-id> <endpoint-id> [group-id] [group-name]
lo-set-scene-group <group-id> <effect-id> <rrggbb> <speed> <brightness> [sequence] [scheduled-start-ms]
lo-sync-clock-group <group-id> [controller-time-ms]
lo-scheduled-scene-group <group-id> <delay-ms> <effect-id> <rrggbb> <speed> <brightness> [sequence]
lo-set-calibration-group <group-id> <time-offset-ms|-> <brightness-cap|-> <palette-override|-> [corr-rrggbb|-] [temp-rrggbb|-]
lo-show-group-help
```

Full operator reference: [`../../docs/console.md`](../../docs/console.md).

## Group Commands

All-node `SetScene`/`SyncClock` activation uses real Matter groupcast once at
least two LED nodes are commissioned. Keep `SetNodeConfig` unicast-only.

Wire encoding (load-bearing): an application **group id** `g` (`0x0001..0xFEFF`,
all-nodes group `0x0001`) is **not** sent as a small id. It is addressed as a
group NodeId `chip::NodeIdFromGroupId(g)` = `0xFFFFFFFFFFFF0000 | g`, which is
what makes `send_invoke_cluster_command` dispatch a groupcast
(`chip::IsGroupId(dest)` true). The `lo-*-group` commands do this encoding; a bare
`0x0001` on a unicast command targets node 1 instead — that is the trap they
avoid.

Enrollment uses the **standard Groups cluster** (not part of this custom
cluster):

- Cluster id `0x0004`, command `0` `AddGroup{ "0:U16": group_id, "1:STR": name }`
  sent unicast to each LED endpoint (the on/off-light endpoint already hosts a
  Groups server). `lo-add-group` wraps this.
- Group **keys** are required for groupcast to be accepted: the controller installs
  a keyset (`controller group-settings add-keyset/bind-keyset/add-group`) and each
  node gets the same key + a group→keyset map via the Group Key Management cluster
  (`0x003F` KeySetWrite + GroupKeyMap). Each node also needs an Access Control
  cluster (`0x001F`) ACL entry for group `0x0001`; otherwise the encrypted
  groupcast is received but rejected with `AccessControl: denied`. The node-side
  key/map/membership/ACL sequence is the hardware-gated step; see
  [`../../docs/console.md`](../../docs/console.md#one-time-group-key--enrollment-setup).
  Do not claim group control works until every node renders one group `SetScene`.

## Program Bundles (Direction)

Kubernetes authors **program bundles** — declarative playlists of `(effect_id,
params, timing)` plus schedules — over the stable effect-id registry above.
Bundles are data, not code; new effect behavior still ships as compiled firmware
via OTA. This extends, and does not replace, the command contract:

- **Distribute then activate.** Push a bundle per node with a unicast transfer
  (BDX-style chunking if it outgrows a single command), then activate
  "bundle vX at time T" with a group command so all segments switch together.
- **Version + reconcile.** Carry a `bundle_id` and `bundle_version`; a node
  reports its last-accepted version as a status attribute so the hub can
  reconcile a node that missed an update. A malformed bundle is rejected and the
  node keeps its last valid program.
- **Autonomous playback.** A node holding a scheduled bundle plus a `SyncClock`
  offset keeps running the timeline through a hub/OTBR outage and re-syncs on
  reconnect.

Exact bundle schema, size limits, transfer mechanism, and signing are open; see
[`../../docs/architecture.md#program-distribution`](../../docs/architecture.md#program-distribution).
