# Roadmap

This roadmap turns the project phases into acceptance criteria. A phase is done
when the listed behavior works on real ESP32-C6 hardware or, for host-only
pieces, through a repeatable local command.

## Phase 1: One ESP32-C6 Drives One Strip

Status: done.

Acceptance criteria:

- Firmware builds for `riscv32imac-unknown-none-elf`.
- One ESP32-C6 drives one WS2812B strip from GPIO2.
- The baked-in scene renders continuously without a controller.
- Effects come from `shared/`, not firmware-local copies.

## Phase 2: Controller To One Node Over WiFi

Status: done.

Acceptance criteria:

- `shared::SetScenePacket` defines a fixed-width `no_std` scene packet.
- `loctl all solid`, `loctl all effect`, and `loctl all off` send UDP packets.
- Firmware joins WiFi when credentials are provided by build-time environment
  variables or ignored `firmware/.env`.
- Firmware listens for UDP datagrams on port `4242`.
- Firmware decodes `SetScenePacket`, ignores packets for other node ids, and
  swaps the active scene after a valid targeted or broadcast packet.
- The board keeps rendering the last valid scene while disconnected.
- Controller-side packet and parsing tests pass.
- ESP32-C6 flashing works over USB.
- Real ESP32-C6 hardware confirmed WiFi join, UDP `4242`, controller command,
  and physical LED response.

Regression commands:

```bash
cd firmware && cargo build --release
cd ../shared && cargo test
cd ../controller && cargo test
```

The Rust WiFi/UDP path remains the known-good fallback while the Matter/Thread
prototype is developed separately.

## Phase 3: ESP-Matter/Thread Feasibility Prototype

Status: in progress. Initial ESP-IDF LED-node and controller-node app
skeletons exist under `matter-prototype/`; first `idf.py` build and hardware
validation remain.

Goal: prove one LED node and one controller node using ESP-Matter over Thread on
ESP32-C6.

Acceptance criteria:

- A separate ESP-IDF/ESP-Matter prototype exists under `matter-prototype/`.
- One ESP32-C6 LED node builds in Thread mode and exposes the LED Orchestra
  custom Matter cluster.
- One ESP32-C6 controller node builds with USB serial operator ingress.
- The controller node commissions the LED node into a private development
  Matter fabric.
- The controller node sends `SetScene` over Matter and the physical LEDs
  change.
- The Rust WiFi/UDP fallback still builds and tests.

Recommended next implementation slice:

1. Set up/export the ESP-IDF/ESP-Matter toolchain for ESP32-C6.
2. Build `matter-prototype/led-node` and fix first-build SDK issues.
3. Build `matter-prototype/controller-node` and fix first-build SDK issues.
4. Flash one LED node and one controller node.
5. Commission one LED node and confirm `lo-set-scene` changes physical LEDs.
6. Confirm controller operation without venue WiFi/router/internet.

## Phase 4: Multi-Node Offline Thread Mesh

Status: planned.

Goal: control multiple ESP32-C6 LED nodes with no venue WiFi/router/internet.

Acceptance criteria:

- At least two LED nodes commission into the private fabric.
- Each node reports identity, firmware version, and current segment metadata.
- Controller node sends group/multicast `SetScene` commands.
- Both physical strips respond to the same group scene command.
- The same control path works after disconnecting venue WiFi/router/internet.
- Missing nodes keep rendering their last valid scene.

## Phase 5: Segment Config And Synchronized Effects Over Matter

Status: planned.

Goal: make the Matter controller node the source of truth for node config and
effect timing.

Acceptance criteria:

- Controller node provisions durable `NodeConfig` with `SetNodeConfig`.
- LED nodes load and log persisted config at boot.
- Controller node sends `SyncClock`.
- LED nodes use scheduled `SetScene` start times for aligned effects.
- At least two nodes render contiguous parts of one virtual strip with visible
  effect continuity across the boundary.
- Existing effect ids stay compatible with the Rust fallback protocol.

## Phase 6: Offline OTA

Status: planned.

Goal: update commissioned LED nodes without USB and without internet.

Acceptance criteria:

- Controller node accepts an OTA image over USB serial.
- LED nodes enable Matter OTA Requestor.
- Controller node serves the image to target LED nodes over the local
  Matter/Thread fabric.
- Prototype images are signed and encrypted.
- Invalid or wrong-key images are rejected.
- Failed updates do not brick a node, and USB flashing remains the recovery
  path.
- Before field use, secure boot, flash encryption, encrypted storage, and key
  handling are enabled and documented.

## Phase 7: Operator UX Beyond USB Serial

Status: planned.

Goal: improve operation after the controller-node USB flow is stable.

Acceptance criteria:

- USB serial remains the reliable recovery/control interface.
- A deliberate next operator surface is selected: private WiFi AP, TUI bridge,
  display/buttons, phone app, or another explicit interface.
- The selected interface can list nodes, switch scenes/effects, provision
  segment config, identify nodes, and start OTA.
- The selected interface does not remove the offline Thread mesh requirement.
