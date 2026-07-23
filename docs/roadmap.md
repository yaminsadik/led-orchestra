# Roadmap

This roadmap turns the project phases into acceptance criteria. A phase is done
when the listed behavior works on real ESP32-C6 hardware or, for host-only
pieces, through a repeatable local command.

The production controller/border-router topology was a validation-gated
decision; see [`controller-topology-adr.md`](controller-topology-adr.md) and
[`controller-topology-validation.md`](controller-topology-validation.md).
**Resolved 2026-06-10:** the offline co-located **S3+H2 one-board hub failed**
its validation gate, so the selected architecture is the **split topology**:
**S3+H2 board as BR-only + separate ESP32-C6 controller/commissioner + C6 LED
nodes**.

## Phase 1: One ESP32-C6 Drives One Strip

Status: done and archived on `archive/rust-phase-2`.

Acceptance criteria:

- Rust firmware builds for `riscv32imac-unknown-none-elf` on the archive branch.
- One ESP32-C6 drives one WS2812B strip from GPIO2.
- The baked-in scene renders continuously without a controller.
- Effects are shared between the archived Rust firmware and host controller.

## Phase 2: Controller To One Node Over Wi-Fi

Status: done and archived on `archive/rust-phase-2`.

Acceptance criteria:

- A fixed-width `no_std` scene packet and `loctl` UDP commands work end to end.
- Firmware joins Wi-Fi, listens on UDP `4242`, decodes targeted/broadcast scene
  packets, and keeps rendering the last valid scene while disconnected.
- Confirmed on real ESP32-C6 hardware (Wi-Fi join, UDP, command, LED response).

The Rust Wi-Fi/UDP path is the historical proof and recovery reference; it is no
longer carried on `main`.

## Phase 3: C++ ESP-Matter/Thread Feasibility

Status: in progress — feasibility established, with one architecture-defining
result.

ESP-IDF LED-node and controller-node apps exist under `matter-prototype/` and
build for ESP32-C6. Confirmed on hardware:

- The controller boots to `LED Orchestra controller node ready` over USB
  serial/JTAG; the standalone operator Wi-Fi softAP starts and stays up.
- OpenThread starts (`device type ROUTER`); the controller joins fabric index 1.
- BLE commissioning completes PASE + `AddNOC`; the LED node attaches to Thread
  and registers its `_matter._tcp` SRP record.

**Resolved open risk (the key Phase 3 finding):** a single infra-less ESP32-C6
cannot act as Matter commissioner *and* its own SRP/DNS-SD owner — operational
discovery times out (`dns browse` → `Error 28: ResponseTimeout`) even though the
SRP record exists and the mesh carries traffic. The controller path therefore
**requires a real OpenThread Border Router**. The cheap `CONFIG_ENABLE_ROUTE_HOOK`
experiment was tried and ruled out. Full evidence:
[`debugging-journal.md`](debugging-journal.md).

Bring-up fix retained: CHIP's `ENABLE_WIFI_AP`/`ENABLE_WIFI_STATION` stay **off**
so the Matter connectivity manager does not seize the radio; the operator AP is a
standalone `esp_wifi` softAP independent of Matter.

Phase 3 exit = the topology validation in Phase 4 selects an option and renders a
scene through a real border router.

## Phase 4: Border-Router Topology Validation

Status: **done — topology selected on hardware (2026-06-10).** The offline
co-located S3+H2 one-board hub failed Stage C; the project selected the proven
**split topology**:
**S3+H2 BR-only + separate ESP32-C6 controller**. This phase ran
[`controller-topology-validation.md`](controller-topology-validation.md) to
decide the controller/border-router architecture.

Goal: prove operational discovery and end-to-end control work through a real
OpenThread Border Router, then select the controller/border-router split that
actually works on hardware.

Acceptance criteria (quantitative — see the validation doc for thresholds):

- A real border router is mandatory; the infra-less single-C6 path is ruled out.
- The **selected topology** proves discovery + operational CASE + physical render
  through a real BR:
  **S3+H2 BR-only + separate C6 controller + C6 LED nodes**.
- The offline co-located **S3+H2 one-board hub failed** the decisive Stage C
  gate, so it is not the active architecture.
- Recovery and later scale/soak work continue on the selected split topology.

Decision result:

```text
S3+H2 BR-only baseline (Stage B) PASSED 2026-06-08
S3+H2 one-board hub (Stage C) FAILED offline 2026-06-09
Selected architecture 2026-06-10 -> S3+H2 BR-only + separate C6 controller
```

## Phase 5: Multi-Node Offline Thread Mesh

Status: **three-node groupcast gate passed on the selected split topology (2026-06-10).** Real
Matter group control is implemented and hardware-proven on LED nodes 2/3 —
`lo-add-group` (Groups cluster AddGroup),
`lo-set-scene-group` / `lo-sync-clock-group` / `lo-scheduled-scene-group` (group
ids encoded with `chip::NodeIdFromGroupId`), plus `lo-show-group-help` for the
one-time group-key sequence. Both ESP32-C6 apps build. The proven node-side
sequence is Group Key Management `0x003F` KeySetWrite + GroupKeyMap, Groups
`AddGroup`, and Access Control `0x001F` ACL for the group subject.

Goal: control multiple LED nodes through the hub over Thread, with no venue
Wi-Fi/router/internet requirement.

Acceptance criteria:

- At least two LED nodes commission into the private fabric and resolve through
  the border router.
- Each node reports identity, firmware version, current segment metadata, and
  last-accepted bundle version.
- The hub sends group/multicast `SetScene`; both strips respond to one group
  command.
- The same control path works with venue Wi-Fi/internet disconnected.
- Missing nodes keep rendering their last valid scene/bundle.

## Phase 6: Segment Config, Synchronized Effects, And Program Bundles

Status: **two-node hardware gate passed 2026-06-26; scale/soak remains.**
All Phase 6 firmware is in place: durable NVS `NodeConfig` (versioned, CRC,
load-and-log at boot), durable NVS scene persistence (last active scene survives
power cycle - `scene persisted ...` / `scene loaded from NVS ...`), scheduled
`SetScene` with keep-last-valid promotion at the synchronized start time, the
`lo-scheduled-scene-group` convenience command, `lo-sync-clock-group`, the
FastLED-shaped effect/color engine + append-only effect metadata registry, and
`lo-read-config` (unicast ReadRequest to query all cluster attributes from a
node). Hardware proof on the selected split topology used the S3+H2 BR-only
board, one C6 controller, and two C6 LED nodes: durable config survived LED-node
reset, both nodes joined group `0x0001`, group `SetScene` sequence `6001`
rendered on both nodes, and synchronized scheduled sequence `6002` activated on
both nodes within about 10 ms. The declarative bundle *format* and on-Thread
transport remain open by design (see the effect-management decision below and
`architecture.md`); the code boundaries (commands append-only, bundles-are-data,
distribute-then-activate) are in place.

Runtime config/calibration (field tuning is data, not firmware): `SetCalibration`
(custom-cluster command `0x03`, append-only) delivers per-node field tuning as
data — a signed synchronized-timing offset (the hole-to-hole / travel-delay
alignment knob, applied to effect math only, never to scheduled-scene
activation), a master-brightness cap, a palette override for palette-driven
effects, and LED color correction / white-point. It is persisted in NVS
(magic/version/CRC, identity defaults) and read back via three `calib_*`
attributes through `lo-read-config`. Delivered with `lo-set-calibration`
(unicast, per-hole) and `lo-set-calibration-group`. This makes the mini-golf
install tunable after mounting with no reflash; only new effect *algorithms* ship
through OTA. The built-in effect set was extended (append-only) with
installation-oriented effects `9`-`13`: ocean drift, color wave (the
calibration-tunable synchronized wave), pulse reveal, celebration, and identify
(install mapping).

Goal: make the hub the source of truth for node config, effect timing, and the
distribution of declarative program bundles.

Effect-management decision:

- FastLED provides the early effect library; do not build a large custom effect
  engine until FastLED is proven insufficient. FastLED runs only on LED nodes.
- "Create" = a compiled C++/FastLED effect with a new stable append-only
  `effect_id`. "Read" = the hub can list effects, firmware version, and parameter
  metadata. "Update" = live params (color, speed, brightness, palette, direction,
  timing); changing effect *code* ships through OTA. "Delete" = hide/deprecate;
  never reuse an `effect_id`.
- Program bundles are **declarative data** (playlists of `(effect_id, params,
  timing)` + schedules), not code. Runtime-uploaded scripts/plugins are out of
  scope.
- **Open decision — declarative renderer/bundle format (do not lock in yet).**
  Bundles will be authored/scheduled declaratively, but the concrete
  serialization is **undecided**: candidates include JSON or YAML for authoring,
  and a compact binary form on the Thread wire. We need to decide based on hub
  footprint, human-authorability, schema/versioning, and parse cost on the LED
  node before committing. Whatever is chosen, the on-wire `SetScene`/bundle
  contract stays append-only (effect ids + cluster field tags never reorder).

Acceptance criteria:

- Hub provisions durable `NodeConfig` with `SetNodeConfig`; nodes load and log
  persisted config at boot.
- Hub sends `SyncClock`; nodes use scheduled `SetScene` start times for aligned
  effects across the segment boundary.
- Bundles are distributed per-node (unicast) then activated by group at a
  scheduled time; nodes store the active bundle and report its version.
- A node holding a scheduled bundle + synced clock keeps running the timeline
  through a controller/OTBR outage and re-syncs on reconnect.
- Effect ids are append-only and compatible across OTA updates.

## Phase 7: Offline OTA

Status: **offline path implemented; provider-on controller boot root-caused +
mostly fixed (2026-06-28). Wi-Fi-init heap fix and end-to-end apply proof remain
(blocked on bench reconnect).** Implemented and built:

- **LED node = OTA Requestor + brick-safe.** `CONFIG_ENABLE_OTA_REQUESTOR=y`, dual
  3 MB OTA slots on the N8 layout, **app rollback enabled**
  (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`) plus a Thread-attach **health gate**
  that confirms a new image only after the mesh attaches, else rolls back. Device
  software version is Kconfig-driven for repeatable OTA version bumps.
- **Controller = offline OTA Provider.** Behind `CONFIG_LED_ORCHESTRA_ENABLE_OTA_PROVIDER`,
  backed by `lo_ota_provider` — our **offline fork** of esp-matter's provider with
  the DCL candidate fetch and forced-TLS download **removed**: `lo-ota-set-image`
  registers a **local** candidate and the bytes stream over **plain HTTP** from a
  hub-local control-LAN endpoint ([`lo-ota-image-server.py`](../matter-prototype/s3-h2-hub-validation/lo-ota-image-server.py));
  laptop now, Kubernetes endpoint later, only the URL changes. The provider-on
  controller builds and fits (about 2.86 MB / 5 MB factory) and the provider
  cluster is confirmed live on endpoint `1`. QueryImage reaches the provider and
  the LED node's `BDX:ReceiveInit` now dispatches to the OTA BDX sender.

**Architecture note found at the provider-on gate:** the provider-on controller
**cannot BLE-commission new nodes** — enabling the Matter server (required for the
provider cluster) claims BLE in the commissionable *peripheral* role, which
conflicts with the commissioner's *central* role (esp-matter BLE is single-role).
This does not block OTA (which rides Thread/CASE, not BLE): the deployment flow is
**commission once with the commissioner build, then OTA over the air**.

**Heap (root-caused 2026-06-28):** the provider-on build runs two full CHIP stacks
(server + commissioner) plus OpenThread + Wi-Fi on one C6 and was over the RAM
budget — `controller.init()` crash-looped with `CHIP_ERROR_NO_MEMORY` (~10 KB free
before the call). Fixed by dropping unneeded subsystems in
`sdkconfig.ota-provider.defaults`: `OPENTHREAD_BORDER_ROUTER=n` (S3+H2 is the BR) +
`BT_ENABLED=n` (this build never BLE-commissions). That frees ~70 KB and
`controller.init()` now succeeds, with the commissioner fabric shared into the
server fabric table (good for OTA). A downstream Wi-Fi-init OOM remains (softAP
buffer shrink staged, **unverified pending bench reconnect**); see the Phase 7
runbook and the 2026-06-28 debugging-journal entry.

**Remaining for functional offline OTA:** confirm the slimmed image boots past
Wi-Fi init, then a commissioned LED node must actually download + apply an image
over Matter/Thread, and the **bad-image rollback** must be confirmed.
**Field-ready** OTA additionally needs secure boot, flash encryption,
signed/encrypted images, and key handling — a separate security layer from fabric
credentials. Full detail + bench runbook:
[`../matter-prototype/s3-h2-hub-validation/phase-7-offline-ota.md`](../matter-prototype/s3-h2-hub-validation/phase-7-offline-ota.md).

Goal: update commissioned LED nodes over the offline Matter/Thread fabric (no
node-side USB cable, no internet).

```text
operator (USB / Wi-Fi) or Kubernetes
  -> signed + encrypted image
  -> S3 hub: stores image, acts as Matter OTA Provider
  -> Matter OTA over Thread
  -> LED nodes: Matter OTA Requestors verify + decrypt + apply
```

Acceptance criteria:

- An image is loaded into the hub over USB serial, controller-local Wi-Fi, or the
  Kubernetes link; the ingress source never joins the fabric.
- The hub stores it and serves it as the Matter OTA Provider over the local
  fabric, with no internet.
- LED nodes pull, verify the signature, decrypt, and apply; invalid/wrong-key
  images are rejected; failed updates do not brick a node (USB recovery).
- New LED modes ship as compiled firmware here. Fabric credentials and image
  signing/encryption are separate security layers (same model for signed
  bundles).
- Before field use: secure boot, flash encryption, encrypted storage, and key
  handling enabled and documented.

## Phase 8: Operator UX And Kubernetes Control Plane

Status: planned.

Goal: formalize how shows are authored and operated on top of the hub. The
control plane never replaces the Thread mesh and never moves the Matter
controller off the hub.

Acceptance criteria:

- Kubernetes authors/validates/schedules bundles and pushes them to the hub over
  a versioned MQTT contract (auth via mTLS/token); the hub stays a thin gateway.
- A telemetry-up path reports node health, current scene, last sequence,
  online/offline, and bundle version back to the control plane.
- USB serial remains the reliable recovery/control interface.
- The selected operator surface can list nodes, switch scenes/effects, provision
  segment config, identify nodes, push bundles, and start OTA.
- No surface adds a hard internet/cloud dependency for rendering already-loaded
  scenes/bundles.
