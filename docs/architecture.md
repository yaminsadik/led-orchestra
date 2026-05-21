# Architecture

LED Orchestra is a distributed LED renderer. Each ESP32-C3 node owns one
physical strip segment, while the controller owns the show state and sends
commands over the network.

## Crates

| Crate | Role |
| --- | --- |
| `shared/` | `no_std` contract shared by firmware and controller: colors, effects, node config, and packet encoding. |
| `firmware/` | ESP32-C3 runtime that renders one segment locally and, from Phase 2 onward, listens for controller packets. |
| `controller/` | Host CLI (`loctl`) that lists effects and sends show commands over UDP. Later phases add state, scenes, and a TUI. |

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

Phase 2 command path:

```text
operator -> loctl -> UDP SetScenePacket -> ESP32-C3 node -> ActiveScene -> LEDs
```

Later phases extend this flow:

```text
operator -> loctl/TUI -> controller state -> resolved ActiveScene + config + sync clock -> nodes
```

The important split is that the controller decides what should happen, while
nodes continue rendering the last valid scene locally if WiFi drops.

## Rendering Invariants

- Effects are pure functions of `(global_index, time_ms, params, context)`.
- Nodes do not need per-effect mutable state to stay visually aligned.
- The controller resolves priority before nodes render.
- A node ignores packets for other node ids.
- A broadcast target node id of `0` applies to every node.
- Firmware keeps the last valid scene if a bad packet arrives or WiFi is lost.

## Override Priority

The planned priority chain is:

```text
emergency > segment > group > global
```

Phase 5 should resolve this chain in the controller, then send a plain
`ActiveScene` to each affected node. That keeps firmware small and predictable.

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

- Firmware WiFi implementation: keep the current `no_std` `esp-hal` direction
  and add the matching ESP radio/network crates for the selected version.
- Config persistence: Phase 3 needs a durable place for `NodeConfig`.
- Time sync: Phase 4 needs a packet type and simple controller-to-node clock
  offset strategy.
- Controller state store: Phase 5 needs a local source of truth for scenes,
  groups, segments, and override state.
