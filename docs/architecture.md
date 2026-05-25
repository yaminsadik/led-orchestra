# Architecture

LED Orchestra is a distributed LED renderer. Each ESP32-C6 node owns one
physical strip segment, while a controller owns the show state and sends
commands over the active network path.

## Crates

| Crate | Role |
| --- | --- |
| `shared/` | `no_std` contract shared by firmware and controller: colors, effects, node config, and packet encoding. |
| `firmware/` | Known-good Rust WiFi/UDP ESP32-C6 runtime that renders one segment and listens for controller packets. |
| `controller/` | Host CLI (`loctl`) that lists effects and sends show commands over UDP for the WiFi fallback path. |
| `matter-prototype/` | ESP-IDF/ESP-Matter prototype lane for Thread LED nodes, a dedicated controller node, custom cluster, and offline OTA. |

The project intentionally has no top-level Cargo workspace. The firmware and
controller target different platforms, use different lock files, and should be
buildable independently.

## Runtime Model

The installation behaves as one virtual strip:

```text
virtual index space: 0 ........................................ total_leds - 1
node 1 segment:      [0, 60)
node 2 segment:              [60, 120)
node 3 segment:                         [120, 180)
```

Each node renders only its own contiguous segment. Effects still receive the
global LED index, so a rainbow or wave can flow across board boundaries.

## Command Flow

Known-good Phase 2 fallback path:

```text
operator -> loctl -> UDP SetScenePacket -> ESP32 node -> ActiveScene -> LEDs
```

Matter/Thread prototype path:

```text
operator -> USB serial -> controller node -> Matter custom cluster over Thread -> LED nodes -> LEDs
```

The important split remains that the controller decides what should happen,
while nodes continue rendering the last valid scene locally if network contact
drops.

## Transport Strategy

The Rust WiFi/UDP path stays intact because it is already confirmed on hardware
and is useful for regression, debugging, and fallback operation.

The new offline mesh path is intentionally separate:

- Matter is the application/security/controller model.
- Thread/OpenThread is the offline IPv6 mesh carried by the ESP32-C6 802.15.4
  radio.
- ESP-Matter is ESP-IDF/C++ oriented, so the first prototype should not rewrite
  or destabilize the Rust firmware.
- The first Matter fabric is private development only, with generated
  per-device factory data and test/dev credentials.

## Rendering Invariants

- Effects are pure functions of `(global_index, time_ms, params, context)`.
- Nodes do not need per-effect mutable state to stay visually aligned.
- Every production LED node is ESP32-C6.
- Effect ids remain stable across the UDP fallback and Matter custom cluster.
- The controller resolves priority before nodes render.
- A node ignores packets for other node ids.
- A broadcast target node id of `0` applies to every node.
- Firmware keeps the last valid scene if a bad command arrives or network
  contact is lost.

## Override Priority

The planned priority chain is:

```text
emergency > segment > group > global
```

Phase 5 should resolve this chain in the controller, then send a plain
`ActiveScene`-equivalent command to each affected node. That keeps firmware
small and predictable.

## Matter Custom Cluster

The Matter/Thread prototype uses a vendor custom cluster instead of trying to
fit LED Orchestra behavior into standard light clusters.

Prototype cluster id: `0xFFF1FC00`.

First commands:

- `SetScene`: effect id, color, speed, brightness, sequence, and scheduled
  start time.
- `SetNodeConfig`: node id, segment range, total LED count, and LED GPIO.
- `SyncClock`: controller time for scheduled scene alignment.

First status attributes:

- Current scene.
- Segment config.
- Firmware version.
- Last accepted sequence.
- Last controller time.

Use Matter group/multicast for all-node scene changes after at least two nodes
are commissioned. Keep provisioning and per-node config unicast-only.

## Adding An Effect

1. Add a new effect implementation under `shared/src/effects/`.
2. Add a stable variant to `EffectId`.
3. Assign a never-reused wire id in `EffectId::wire_id`.
4. Add the reverse mapping in `EffectId::from_wire_id`.
5. Register the effect in `EffectRegistry`.
6. Verify `cd shared && cargo test` and `cd controller && cargo test`.

Do not reorder or reuse wire ids. Nodes and controllers may be updated at
different times, especially after OTA support exists.

## Open Design Choices

- Exact Matter vendor id/product id and cluster ids for production.
- Whether the dedicated controller node can stay Thread-only or needs an
  explicit OpenThread border-router configuration.
- Durable storage layout for Matter-provisioned `NodeConfig`.
- OTA image storage limits on the controller node.
- Operator UX after the USB serial prototype proves stable.
