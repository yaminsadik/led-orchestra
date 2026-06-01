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
`matter-prototype/` and build for ESP32-C6. One LED-node image has been flashed.
The controller-node image now boots with both operator ingress paths confirmed on
hardware: the USB serial/JTAG controller shell reaches `LED Orchestra controller
node ready`, and the controller-local private Wi-Fi AP ingress works end to end (a
laptop/phone can see and join the AP). Commissioning and end-to-end Matter/Thread
LED control remain.

Confirmed on hardware so far:

- Controller boots to `LED Orchestra controller node ready` over USB serial/JTAG.
- The standalone operator Wi-Fi softAP (SSID/password from the gitignored
  `sdkconfig.defaults.local`, channel 6) starts, stays up, and is
  visible/joinable from a laptop.
- OpenThread starts (`device type ROUTER`) and the controller joins fabric
  index 1.

Key fix found during hardware bring-up:

- CHIP's `ENABLE_WIFI_AP`/`ENABLE_WIFI_STATION` must stay **off** on the
  controller. With `ENABLE_WIFI_AP` on, `CHIP_DEVICE_CONFIG_ENABLE_WIFI` became
  non-zero, so the Matter connectivity manager seized the Wi-Fi radio, forced it
  to STA, and stopped the operator AP (`WIFI_EVENT_AP_STOP`) right after boot. The
  operator AP is now a standalone `esp_wifi` softAP
  (`matter-prototype/controller-node/main/controller_wifi_ingress.cpp`),
  independent of Matter, which runs over Thread.

Open issue surfaced by this work:

- With CHIP Wi-Fi off and no Thread SRP server / border router present, Matter
  DNS-SD advertising over IP errors at boot (`chip[DIS]: Failed to advertise
  ... : 3`). Offline Thread discovery is expected to ride Thread SRP; this is
  direct evidence for the open architecture risk below and must be resolved while
  confirming the embedded-controller-over-Thread path.

Goal: prove one LED node and one controller node using C++ ESP-Matter over
Thread on ESP32-C6.

Acceptance criteria:

- `main` is C++/ESP-IDF oriented; Rust Phase 1/2 code stays on
  `archive/rust-phase-2`.
- One ESP32-C6 LED node builds in Thread mode and exposes the LED Orchestra
  custom Matter cluster.
- One ESP32-C6 controller node builds with USB serial operator ingress and
  controller-local Wi-Fi ingress. The default Wi-Fi mode is a private controller
  AP; station mode is a deliberate build-time option for private/local networks.
- The controller node commissions the LED node into a private development
  Matter fabric.
- The controller node sends `SetScene` over Matter and the physical LEDs
  change.
- A FastLED integration spike confirms whether FastLED can replace the current
  ESP-IDF `led_strip` renderer inside the ESP-Matter app on ESP32-C6.
- Hardware validation answers the open risk: whether ESP-Matter supports a
  Thread-side embedded controller on ESP32-C6, or whether the controller path
  needs an explicit OpenThread Border Router role. Either outcome must keep LED
  nodes controlled by the controller over Thread, with no internet requirement.

Recommended next implementation slice:

1. ~~Open the controller serial monitor and confirm the AP-enabled image
   boots.~~ Done: boots to `LED Orchestra controller node ready`.
2. ~~Confirm the controller private operator AP appears.~~ Done: the AP is
   visible and joinable from a laptop and stays up after the CHIP Wi-Fi fix.
3. Resolve the Matter DNS-SD/Thread-SRP discovery errors (see open issue above)
   far enough to attempt commissioning, or confirm commissioning works over BLE
   despite them.
4. Commission the flashed LED node into the private development fabric.
5. Run `lo-set-scene` and confirm physical LEDs change over Matter/Thread.
6. Confirm controller operation without venue WiFi/router/internet. USB remains
   the baseline ingress; controller-local Wi-Fi is allowed only as an operator
   ingress path to the controller node.
7. Spike FastLED inside the LED-node app and choose the first stable renderer
   backend for Phase 4.

## Phase 4: Multi-Node Offline Thread Mesh

Status: planned.

Goal: control multiple ESP32-C6 LED nodes through the controller node over
Thread, with no venue WiFi/router/internet requirement.

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

Effect-management decision:

- FastLED should provide the early effect library; do not build a large custom
  effect engine until FastLED is proven insufficient.
- "Create" means adding a compiled C++/FastLED effect with a new stable
  append-only `effect_id`.
- "Read" means the controller can list supported effects, firmware version, and
  basic parameter metadata.
- "Update" means changing live parameters such as color, speed, brightness,
  palette, direction, and timing; changing effect code ships through OTA.
- "Delete" should normally mean hide or deprecate an effect. Never reuse an old
  `effect_id` for different behavior.
- Runtime-uploaded effect scripts/plugins are not part of Phase 5 or Phase 6.

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

Goal: update commissioned LED nodes over the offline Matter/Thread fabric (no
node-side USB cable, no internet).

Flow:

```text
operator (laptop/mobile)
  -> USB serial or controller-local Wi-Fi: signed + encrypted image
  -> controller node: stores image, acts as Matter OTA Provider
  -> Matter OTA over Thread
  -> LED nodes: Matter OTA Requestors verify + decrypt + apply
```

The operator's laptop/phone is only ingress for the image bytes; it never joins
the Matter fabric. USB serial is the required recovery/baseline path, and
controller-local Wi-Fi is an allowed convenience path. Matter fabric credentials
(who may use the OTA cluster) and firmware image signing/encryption (what
firmware a node will run) are separate security layers.

Acceptance criteria:

- The operator loads a signed and encrypted OTA image into the controller node
  over USB serial or controller-local Wi-Fi.
- The controller node stores the image and acts as the Matter OTA Provider.
- LED nodes enable Matter OTA Requestor and pull from the provider.
- The controller node serves the image to target LED nodes over the local
  Matter/Thread fabric, with no internet or venue Wi-Fi.
- LED nodes verify the signature and decrypt before applying.
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

This phase formalizes additional operator surfaces (private Wi-Fi AP, phone app,
etc.) as **operator ingress**, layered on top of the controller node. They never
replace the Thread mesh and never move the Matter controller off the ESP32-C6
controller node.

Acceptance criteria:

- USB serial remains the reliable recovery/control interface.
- A deliberate next operator surface is selected: private Wi-Fi AP, TUI bridge,
  display/buttons, phone app, or another explicit interface — strictly as UI
  ingress to the controller node, not as a new Matter controller.
- The selected interface can list nodes, switch scenes/effects, provision
  segment config, identify nodes, and start OTA.
- The selected interface does not remove the offline Thread mesh requirement and
  does not add a hard internet/cloud dependency.
