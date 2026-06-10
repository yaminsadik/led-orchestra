# Roadmap

This roadmap turns the project phases into acceptance criteria. A phase is done
when the listed behavior works on real ESP32-C6 hardware or, for host-only
pieces, through a repeatable local command.

The production controller/border-router topology is locked as a validation-gated
decision; see [`controller-topology-adr.md`](controller-topology-adr.md) and
[`controller-topology-validation.md`](controller-topology-validation.md).
**Amended 2026-06-06:** "Hub" below means the **S3+H2 one-board hub** (Matter
controller + esp-thread-br host on the ESP32-S3 + ESP32-H2 RCP + thin Kubernetes
gateway); the all-C6 split is the proven fallback.

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

Status: **in progress — Stage 0 (all-C6) PASSED on hardware (2026-06-04). Amended
2026-06-06: the S3+H2 one-board hub is now the primary candidate; Stages A-F next.**
This phase runs
[`controller-topology-validation.md`](controller-topology-validation.md) and
selects the S3+H2 hub, the all-C6 split, or the Pi fallback.

Goal: prove operational discovery and end-to-end control work through a real
OpenThread Border Router, and decide where the controller co-locates.

Acceptance criteria (quantitative — see the validation doc for thresholds):

- **Stage 0 (all-C6, primary go/no-go for the fallback) — [PASSED 2026-06-04]:**
  a *separate* Thread client resolved an LED node's `_matter._tcp` record through
  the BR (host + RCP) — not the BR resolving its own record. `dns browse` returned
  the record instead of `Error 28`, and operational CASE then completed (a
  `SetScene` rendered) once the commissioner's Wi-Fi softAP was dropped to clear
  single-radio contention.
- **S3+H2 hub (primary target), Stages A-F:** inventory/toolchain (A); S3+H2 as a
  BR-only baseline resolving for the separate C6 client (B); the **one-node
  end-to-end gate** where the co-located S3 commissions one C6 LED, resolves it
  through its own H2-backed BR, and renders `SetScene` over the custom cluster
  (C); repeatability/recovery (D); ~20-node scale + ≥ 72 h soak (E); thin ingress
  last (F).
- Metrics hold at ~20-node scale and through a ≥ 72 h soak with ≥ 20 hard power
  cycles: min free heap, largest free block, no heap drift, 100% reboot recovery
  and discovery, flash headroom (the S3+H2 board is 8 MB + 2 MB PSRAM).
- A thin Kubernetes bundle gateway is added last and all metrics still pass.

Decision mapping:

```text
Stage 0 (all-C6 split) PASSED 2026-06-04 (discovery + operational CASE).
S3+H2 hub Stages A-F PASS within headroom -> S3+H2 one-board hub  [primary target]
S3+H2 hub FAILS (one-board discovery OR heap/stability) -> all-C6 split (= Stage 0 config)
C6/H2 esp-thread-br path itself not stable -> Pi / ot-br-posix
```

## Phase 5: Multi-Node Offline Thread Mesh

Status: **two-node groupcast gate passed on the split topology (2026-06-10).** Real
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

Status: **core firmware landed; hardware gate + bundle transport pending.**
Durable NVS `NodeConfig` (versioned, CRC, load-and-log at boot), scheduled
`SetScene` with keep-last-valid promotion at the synchronized start time, the
`lo-scheduled-scene-group` convenience command, and the FastLED-shaped
effect/color engine + append-only effect metadata registry are implemented and
build. The declarative bundle *format* and on-Thread transport remain open by
design (see the effect-management decision below and `architecture.md`); the code
boundaries (commands append-only, bundles-are-data, distribute-then-activate) are
in place. Not gated until multi-node hardware confirms synchronized scheduled
group activation.

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

Status: **requestor present; provider scaffold landed (build-gated); offline
image plumbing + field-security pending.** LED nodes are Matter OTA Requestors
(`CONFIG_ENABLE_OTA_REQUESTOR=y`, requestor cluster auto-created, `ota_0`/`ota_1`/
`otadata` partitions present; builds). The controller has an OTA Provider cluster
+ `lo-ota-*` operator commands behind `CONFIG_LED_ORCHESTRA_ENABLE_OTA_PROVIDER`
(off by default). **Remaining for functional offline OTA:** the stock provider
sources candidates from DCL and fetches image bytes over HTTP, so a hub-local
image endpoint (or a flash-backed provider extension) is needed to keep it
offline; details in
[`../matter-prototype/s3-h2-hub-validation/phase-7-offline-ota.md`](../matter-prototype/s3-h2-hub-validation/phase-7-offline-ota.md).
OTA is **functional** only after a real LED node downloads and applies an image
over Matter/Thread. **Field-ready** OTA additionally needs secure boot, flash
encryption, signed/encrypted images, key handling, and rollback/recovery tests —
a separate security layer from fabric credentials.

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
