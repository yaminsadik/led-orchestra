# Roadmap

This roadmap turns the project phases into acceptance criteria. A phase is done
when the listed behavior works on real ESP32-C6 hardware or, for host-only
pieces, through a repeatable local command.

## Phase 1: One ESP32-C6 Drives One Strip

Status: done and archived on `archive/rust-phase-2`.

Acceptance criteria:

- Rust firmware builds for `riscv32imac-unknown-none-elf` on the archive
  branch.
- One ESP32-C6 drives one WS2812B strip from GPIO2.
- The baked-in scene renders continuously without a controller.
- Effects are shared between the archived Rust firmware and host controller.

## Phase 2: Controller To One Node Over WiFi

Status: done and archived on `archive/rust-phase-2`.

Acceptance criteria:

- `shared::SetScenePacket` defines a fixed-width `no_std` scene packet.
- `loctl all solid`, `loctl all effect`, and `loctl all off` send UDP packets.
- Firmware joins WiFi when credentials are provided by build-time environment
  variables or the archive branch's ignored local env file.
- Firmware listens for UDP datagrams on port `4242`.
- Firmware decodes `SetScenePacket`, ignores packets for other node ids, and
  swaps the active scene after a valid targeted or broadcast packet.
- The board keeps rendering the last valid scene while disconnected.
- Controller-side packet and parsing tests pass.
- ESP32-C6 flashing works over USB.
- Real ESP32-C6 hardware confirmed WiFi join, UDP `4242`, controller command,
  and physical LED response.

The Rust WiFi/UDP path remains the historical proof and recovery reference, but
it is no longer carried on `main`.

## Phase 3: C++ ESP-Matter/Thread Feasibility Prototype

Status: in progress. ESP-IDF LED-node and controller-node apps exist under
`matter-prototype/` and build for ESP32-C6. One LED-node image has been flashed;
controller-node flashing, commissioning, and end-to-end Matter/Thread LED
control remain.

Goal: prove one LED node and one controller node using C++ ESP-Matter over
Thread on ESP32-C6.

Acceptance criteria:

- `main` is C++/ESP-IDF oriented; Rust Phase 1/2 code stays on
  `archive/rust-phase-2`.
- One ESP32-C6 LED node builds in Thread mode and exposes the LED Orchestra
  custom Matter cluster.
- One ESP32-C6 controller node builds with USB serial operator ingress.
- The controller node commissions the LED node into a private development
  Matter fabric.
- The controller node sends `SetScene` over Matter and the physical LEDs
  change.
- A FastLED integration spike confirms whether FastLED can replace the current
  ESP-IDF `led_strip` renderer inside the ESP-Matter app on ESP32-C6.

Recommended next implementation slice:

1. Plug in the controller ESP32-C6 and detect its serial port.
2. Flash `matter-prototype/controller-node`.
3. Open the controller serial monitor.
4. Commission the flashed LED node into the private development fabric.
5. Run `lo-set-scene` and confirm physical LEDs change over Matter/Thread.
6. Confirm controller operation without venue WiFi/router/internet.
7. Spike FastLED inside the LED-node app and choose the first stable renderer
   backend for Phase 4.

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

## Phase 5: Segment Config And Synchronized FastLED Effects Over Matter

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
- Effect ids are append-only and remain compatible across OTA updates.
- FastLED-backed effects can be added in C++ without changing the Matter command
  contract.

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
- New LED modes are delivered as compiled firmware updates in this phase.
  Runtime-loadable effect plugins/scripts are a separate future design, not a
  Phase 6 requirement.
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
