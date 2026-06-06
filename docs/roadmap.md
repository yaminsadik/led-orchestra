# Roadmap

This roadmap turns the project phases into acceptance criteria. A phase is done
when the listed behavior works on real ESP32-C6 hardware or, for host-only
pieces, through a repeatable local command.

The production controller/border-router topology is locked as a validation-gated
decision; see [`controller-topology-adr.md`](controller-topology-adr.md) and
[`controller-topology-validation.md`](controller-topology-validation.md). "Hub"
below means the Option 2 Hub C6 (Matter controller + esp-thread-br host + thin
Kubernetes gateway).

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

Status: **in progress — Stage 0 passed on hardware (2026-06-04); Stage 1 next.**
This phase runs
[`controller-topology-validation.md`](controller-topology-validation.md) and
selects Option 2, 3, or 4.

Goal: prove operational discovery and end-to-end control work through a real
OpenThread Border Router, and decide where the controller co-locates.

Acceptance criteria (quantitative — see the validation doc for thresholds):

- **Stage 0 (primary go/no-go) — [PASSED 2026-06-04]:** a *separate* Thread
  client resolved an LED node's `_matter._tcp` record through the BR (host + RCP)
  — not the BR resolving its own record. `dns browse` returned the record instead
  of `Error 28`, and operational CASE then completed (a `SetScene` rendered) once
  the commissioner's Wi-Fi softAP was dropped to clear single-radio contention.
- From Stage 1 the controller is **co-located onto the BR host** (the Hub
  candidate) and commissions one LED node through operational CASE — resolving
  through its on-board BR — then renders `SetScene` over the custom cluster.
- Metrics hold at ~20-node scale and through a ≥ 72 h soak with ≥ 20 hard power
  cycles: min free heap, largest free block, no heap drift, 100% reboot recovery
  and discovery, flash headroom (hub sized to 8/16 MB as needed).
- A thin Kubernetes bundle gateway is added last and all metrics still pass.

Decision mapping:

```text
Stage 0 FAIL (separate client can't resolve through the BR) -> Option 4 (Pi / ot-br-posix)
Stage 1+ co-located Hub FAILS (on-board discovery OR heap/stability) -> Option 3 (split, all-C6)
Stages 0-4 PASS within headroom -> Option 2 (Hub C6 + RCP)
```

## Phase 5: Multi-Node Offline Thread Mesh

Status: planned.

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

Status: planned.

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

Status: planned.

Goal: update commissioned LED nodes over the offline Matter/Thread fabric (no
node-side USB cable, no internet).

```text
operator (USB / Wi-Fi) or Kubernetes
  -> signed + encrypted image
  -> Hub C6: stores image, acts as Matter OTA Provider
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
