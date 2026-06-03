# Matter/Thread Prototype Plan

This document captures the ESP32-C6-only offline mesh direction. The completed
Rust WiFi/UDP Phase 1/2 implementation is archived on
`archive/rust-phase-2`; `main` now carries the C++ ESP-IDF/ESP-Matter path.

LED control stays fully local: control plane / operator UI -> ESP32-C6 hub ->
Thread/Matter mesh -> ESP32-C6 LED nodes. USB serial is the baseline operator
ingress, controller-local Wi-Fi is allowed for laptop/mobile convenience, and a
Kubernetes control plane pushes program bundles to the hub over IP. There is no
venue Wi-Fi, Ethernet, cloud, or internet requirement to render scenes. See
[`architecture.md`](architecture.md) for the role glossary and topology diagram;
those role definitions (control plane vs. UI/operator ingress vs. Matter
controller vs. Thread Border Router vs. RCP vs. OTA Provider/Requestor) apply
throughout this document.

The production controller/border-router topology is a validation-gated decision
(Option 2 Hub C6 + RCP, with all-C6 Option 3 and Pi Option 4 fallbacks). The
canonical statement and the experiment that selects it are in
[`controller-topology-adr.md`](controller-topology-adr.md) and
[`controller-topology-validation.md`](controller-topology-validation.md).

Current state: ESP-IDF apps exist under `matter-prototype/` for both the LED
node and the controller node; both build for ESP32-C6. The controller boots and
runs its operator AP; BLE commissioning completes PASE + `AddNOC`. Hardware
bring-up established that a single infra-less C6 cannot self-resolve operational
nodes, so the next work is the border-router topology validation (Phase 4).

## Decision

Use ESP-Matter over Thread for the next prototype:

- Matter provides the application model, commissioning, fabric security,
  controller roles, group control, and OTA flow.
- Thread/OpenThread provides the offline IPv6 mesh over the ESP32-C6 802.15.4
  radio.
- The first implementation is a private development fabric, not a production
  Matter certification effort.
- The Matter controller/commissioner is the dedicated ESP32-C6 controller node,
  not the laptop or phone. Laptop/mobile clients are only UI/operator ingress
  and reach the controller node over USB serial or the controller's private
  Wi-Fi AP; they hold no Matter identity.

The prototype is C++ because ESP-Matter and the intended FastLED rendering
stack are C++-native.

## Prototype Roles

### Operator UI / Ingress

- USB serial plus controller-local Wi-Fi private AP by default for laptop/mobile
  convenience.
- Sends operator intent and, in the OTA phase, OTA image bytes to the hub.
- Holds **no** Matter fabric credentials and is **not** the Matter controller.
  It can be unplugged once the controller node holds the desired state.

### LED Node

- ESP32-C6 Matter-over-Thread device.
- Owns one physical WS2812B segment.
- Implements a vendor custom LED Orchestra cluster.
- Renders the last valid scene locally when the controller is unreachable.
- Uses the existing ESP-IDF `led_strip` renderer until a FastLED spike proves
  the final C++ rendering path inside the ESP-Matter app.
- Acts as a **Matter OTA Requestor** in the OTA phase (downloads, verifies,
  decrypts, and applies images from the controller-node provider).

### Controller Node (evolves into the Hub)

The controller node is the Matter controller/commissioner in the private fabric
and the local source of truth for scenes, node inventory, groups, and (later)
OTA images. Hardware bring-up established it needs a real OpenThread Border
Router, so under the locked decision it evolves into the **Hub C6** (Option 2):
Matter controller + esp-thread-br host (with an RCP C6 for the radio) + a thin
Kubernetes bundle gateway. See
[`controller-topology-adr.md`](controller-topology-adr.md).

- Receives operator intent and OTA image bytes over USB serial or the controller
  private AP, and validated program bundles from Kubernetes over IP, then
  translates them into Matter actions on the fabric.
- Caches already-approved bundles and relays them; heavy authoring/validation/
  scheduling stays in Kubernetes — the hub stays thin.
- Sends unicast commands for per-node provisioning and bundle distribution.
- Sends Matter group/multicast commands for "all nodes" scene/bundle activation.
- Acts as the local **Matter OTA Provider** for offline installs.
- Runs without venue Wi-Fi or internet for LED control. Wi-Fi/IP carries only
  Kubernetes ingress and telemetry; LED nodes are controlled over Thread.

## Custom Cluster Contract

Use a vendor custom cluster for LED Orchestra-specific behavior. Keep standard
Matter light clusters out of the first prototype unless compatibility becomes a
separate requirement.

Commands:

- `SetScene`: `effect_id`, `red`, `green`, `blue`, `speed`, `brightness`,
  `sequence`, `scheduled_start_time_ms`.
- `SetNodeConfig`: `node_id`, `segment_start`, `segment_len`, `total_leds`,
  `led_gpio`.
- `SyncClock`: `controller_time_ms`.

Status attributes:

- `current_scene`
- `segment_start`
- `segment_len`
- `total_leds`
- `led_gpio`
- `firmware_version`
- `last_sequence`

Compatibility rules:

- Preserve existing shared effect ids: `0 = off`, `1 = solid`, `2 = rainbow`.
- Add new effect ids only at the end.
- Keep the controller responsible for resolving scene priority before sending
  commands to LED nodes.
- Deliver new compiled LED modes through firmware OTA. Runtime effect plugins or
  scripts are a separate future design.
- Program bundles (playlists/schedules authored in Kubernetes) are **declarative
  data** over the stable effect-id registry, not code. Distribute per-node
  (unicast), activate by group at a scheduled time, and carry a bundle id/version
  reported back as a status attribute. Keep bundles small; use a BDX-style
  chunked transfer if a payload outgrows the cluster.
- Treat effect CRUD conservatively: list effects and update parameters at
  runtime; add or change effect code through OTA; hide/deprecate old effects
  instead of reusing their ids.

Prototype implementation notes:

- Cluster id is currently `0xFFF1FC00`, using a development vendor id.
- The LED node currently renders `off`, `solid`, and `rainbow` using ESP-IDF
  `led_strip`.
- FastLED should be evaluated as a C++ component before replacing `led_strip`.
  Its ESP-IDF CMake path currently expects Arduino-ESP32 integration, so this
  needs explicit build and hardware validation with ESP-Matter on ESP32-C6.
- `SetNodeConfig` is RAM-only in the prototype. Durable NVS storage is planned
  for a later phase.
- The controller node registers USB shell helpers: `lo-set-scene`,
  `lo-set-node-config`, and `lo-sync-clock`.
- **Border-router decision (resolved 2026-06-02).** A single native-Thread
  ESP32-C6 cannot be a self-contained Matter commissioner: BLE commissioning and
  SRP registration succeed, but operational discovery times out (`dns browse` →
  `Error 28: ResponseTimeout`) because an infra-less SoC has no mDNS/advertising
  proxy to answer its own DNS-SD probes. The `CONFIG_ENABLE_ROUTE_HOOK=y`
  experiment was tried and ruled out. The controller path therefore requires a
  real OpenThread Border Router; how much of the controller co-locates with it is
  the validation-gated Option 2/3/4 choice. See
  [`controller-topology-adr.md`](controller-topology-adr.md),
  [`controller-topology-validation.md`](controller-topology-validation.md), and
  [`debugging-journal.md`](debugging-journal.md). Station mode remains a
  deliberate build-time option for a private/local network without making LED
  nodes Wi-Fi devices or adding an internet requirement.

## Security And Manufacturing

Prototype defaults:

- Use generated per-device factory data.
- Use private/test VID/PID values.
- Use unique discriminator, passcode, serial number, and SPAKE2+ verifier per
  board.
- Use test attestation trust and test NOC issuer for the first private fabric.

Before any field/production use:

- Replace test credentials with a real Matter identity path.
- Protect device attestation keys.
- Enable secure boot.
- Enable flash encryption and encrypted secure storage/NVS as appropriate.
- Define a repeatable manufacturing partition process.

## OTA Direction

Prototype OTA target (the OTA phase), fully offline:

```text
operator (USB / controller-local Wi-Fi) or Kubernetes
  -> signed + encrypted firmware image
  -> hub: stores image, acts as Matter OTA Provider
  -> Matter OTA over Thread
  -> LED nodes: Matter OTA Requestors verify + decrypt + apply
```

- The image is loaded over USB serial, controller-local Wi-Fi, or the Kubernetes
  link. The ingress source is only ingress and never joins the Matter fabric.
- The hub stores that image and serves it to commissioned LED nodes as the local
  **Matter OTA Provider** over the offline Matter/Thread fabric. No image is
  fetched from the internet.
- LED nodes act as **Matter OTA Requestors**: download, verify signature,
  decrypt, and apply, keeping USB flashing as the recovery path.
- Reject invalid or wrong-key images and verify rollback/recovery.

### Two Separate Security Layers

OTA security is built from two independent layers; do not collapse them:

1. **Matter fabric credentials** — authorize *who* may participate in the fabric
   and invoke the OTA cluster (controller node as provider, LED nodes as
   requestors).
2. **Firmware image signing/encryption** — authorize *what* firmware a node will
   decrypt, verify, and run.

A valid fabric member still cannot push firmware a node will not verify, and a
correctly signed image cannot be delivered by a device outside the fabric. The
operator's laptop/phone sits outside both layers: it only carries image bytes to
the provider over USB serial or controller-local Wi-Fi.

For development, use private/test Matter credentials and generated factory data.
Before production or public Matter ecosystem use, move to a proper Matter
identity, protected attestation keys, secure boot, flash encryption, and
documented manufacturing/certification choices.

## References

- ESP-Matter for ESP32-C6:
  https://docs.espressif.com/projects/esp-matter/en/latest/esp32c6/
- Developing with ESP-Matter:
  https://docs.espressif.com/projects/esp-matter/en/latest/esp32c6/developing.html
- ESP-Matter controller:
  https://docs.espressif.com/projects/esp-matter/en/latest/esp32c6/controller.html
- ESP-Matter production considerations:
  https://docs.espressif.com/projects/esp-matter/en/latest/esp32c6/production.html
- ESP-Matter security considerations:
  https://docs.espressif.com/projects/esp-matter/en/latest/esp32c6/security.html
- ESP-IDF OpenThread:
  https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/api-reference/network/esp_openthread.html
