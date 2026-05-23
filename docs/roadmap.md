# Roadmap

This roadmap turns the README phases into acceptance criteria. A phase is done
when the listed behavior works on real hardware or, for controller-only pieces,
through a repeatable local command.

## Phase 1: One ESP32-C3 Drives One Strip

Status: done.

Acceptance criteria:

- Firmware builds for `riscv32imc-unknown-none-elf`.
- One ESP32-C3 drives one WS2812B strip from GPIO2.
- The baked-in scene renders continuously without a controller.
- Effects come from `shared/`, not firmware-local copies.

## Phase 2: Controller To One Node Over WiFi

Status: in progress.

Already done:

- `shared::SetScenePacket` defines a fixed-width `no_std` scene packet.
- `loctl all solid`, `loctl all effect`, and `loctl all off` send UDP packets.
- Firmware joins WiFi when credentials are provided by build-time environment
  variables or an ignored `firmware/.env` file.
- Firmware listens for UDP datagrams on port `4242`.
- Firmware decodes `SetScenePacket`, ignores packets for other node ids, and
  swaps the active scene after a valid targeted or broadcast packet.
- The board keeps rendering the last valid scene while disconnected.
- Controller-side packet and parsing tests pass.
- Real ESP32-C3 flash succeeds, and the firmware accepts UDP scene packets over
  LAN broadcast.
- Firmware can be built separately for ESP32-C3 and ESP32-C6 from the same
  source tree.

Remaining acceptance criteria:

- Confirm a real controller command changes the physical LEDs.
- An operator can run:

```bash
cd controller
cargo run -- --bus udp://NODE_IP:4242 --target-node 1 all solid ff0000
cargo run -- --bus udp://NODE_IP:4242 --target-node 1 all effect rainbow
cargo run -- --bus udp://NODE_IP:4242 --target-node 1 all off
cargo run -- --bus udp://192.168.1.255:4242 --target-node 1 all solid ff0000
```

Recommended next implementation slice:

1. Confirm the physical strip changes after the accepted UDP packets.
2. Use LAN broadcast if unicast reports `No route to host`.
3. Flash and smoke-test the ESP32-C6 build on one board.
4. Mark Phase 2 done after the physical LEDs respond.

## Phase 3: Per-Node Segment Config For 20 Boards

Goal: one firmware codebase can provision every node, with separate chip builds
where the hardware target requires them.

Acceptance criteria:

- Each board has a durable `NodeConfig`.
- The controller can provision or update node id, segment id, segment start,
  segment length, total LEDs, and LED GPIO strategy.
- Firmware logs its loaded config at boot.
- Controller can list known segments and nodes.
- At least two physical boards render different contiguous parts of one
  virtual strip.

## Phase 4: Global Synced Effects

Goal: animations line up across board boundaries.

Acceptance criteria:

- Controller sends a shared time origin or clock offset.
- Firmware renders effects with synchronized `time_ms`.
- Two or more nodes show a continuous rainbow/wave across segment boundaries.
- Nodes resync after reconnect.

## Phase 5: Modes, Groups, Scenes, Override Priority

Goal: controller becomes the source of truth for show state.

Acceptance criteria:

- Controller stores global, group, segment, and emergency layers.
- Controller resolves `emergency > segment > group > global`.
- Scenes can be saved, listed, and loaded.
- Segment and group overrides can be cleared.
- Firmware still receives a simple resolved `ActiveScene`.

## Phase 6: Terminal Control Panel

Goal: interactive operation without memorizing commands.

Acceptance criteria:

- Ratatui panel shows nodes, segments, current scene, and online status.
- Operator can switch global effects and colors.
- Operator can identify a segment.
- Operator can clear overrides.
- Panel remains usable while nodes appear and disappear.

## Phase 7: OTA Firmware Updates

Goal: update nodes without USB.

Acceptance criteria:

- Controller can target one node, a group, or all nodes.
- Firmware validates image metadata before applying an update.
- Failed updates do not brick a node.
- Node reports firmware version after reboot.
- Documentation includes a recovery path using USB flashing.
