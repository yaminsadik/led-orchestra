# Architecture

LED Orchestra is a distributed LED renderer. Each ESP32-C6 node owns one
physical strip segment, while a controller owns the show state and sends
commands over the active network path.

## Project Layout

| Path | Role |
| --- | --- |
| `matter-prototype/` | ESP-IDF/ESP-Matter prototype lane for Thread LED nodes, a dedicated controller node, custom cluster, and offline OTA. |
| `matter-prototype/led-node/` | C++ LED-node app that exposes the LED Orchestra custom cluster and renders one physical strip segment. |
| `matter-prototype/controller-node/` | C++ controller/commissioner app with USB serial operator ingress. |
| `matter-prototype/common/` | Shared C++ constants for the custom cluster and effect ids. |

The completed Rust WiFi/UDP Phase 1/2 implementation is archived on the
`archive/rust-phase-2` branch. `main` should stay focused on the C++
ESP-IDF/ESP-Matter path.

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

```text
operator -> USB serial -> controller node -> Matter custom cluster over Thread -> LED nodes -> LEDs
```

The important split remains that the controller decides what should happen,
while nodes continue rendering the last valid scene locally if network contact
drops.

## Transport Strategy

- Matter is the application/security/controller model.
- Thread/OpenThread is the offline IPv6 mesh carried by the ESP32-C6 802.15.4
  radio.
- ESP-Matter is ESP-IDF/C++ oriented, so Phase 3 onward is implemented in C++.
- FastLED is the intended rendering/effect library after an ESP32-C6 +
  ESP-Matter integration spike proves the build/runtime path.
- The first Matter fabric is private development only, with generated
  per-device factory data and test/dev credentials.

## Rendering Invariants

- Effects are pure functions of `(global_index, time_ms, params, context)`.
- Nodes do not need per-effect mutable state to stay visually aligned.
- Every production LED node is ESP32-C6.
- Effect ids are append-only and remain stable across Matter commands and OTA
  updates.
- The controller resolves priority before nodes render.
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

1. Add a stable C++ effect id at the end of the effect-id list.
2. Add the LED-node renderer implementation, preferably using FastLED once the
   integration spike is accepted.
3. Add controller command parsing/help if the effect needs new parameters.
4. Keep the Matter custom-cluster command contract stable unless the new effect
   truly needs new fields.
5. Build both ESP-IDF apps and validate the effect on physical LEDs.

Do not reorder or reuse wire ids. Nodes and controllers may be updated at
different times, especially after OTA support exists.

## Open Design Choices

- Exact Matter vendor id/product id and cluster ids for production.
- Whether the dedicated controller node can stay Thread-only or needs an
  explicit OpenThread border-router configuration.
- Durable storage layout for Matter-provisioned `NodeConfig`.
- OTA image storage limits on the controller node.
- Operator UX after the USB serial prototype proves stable.
