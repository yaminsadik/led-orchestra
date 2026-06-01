# Matter/Thread Prototype Plan

This document captures the ESP32-C6-only offline mesh direction. The completed
Rust WiFi/UDP Phase 1/2 implementation is archived on
`archive/rust-phase-2`; `main` now carries the C++ ESP-IDF/ESP-Matter path.

The first prototype keeps LED control fully local: operator UI -> ESP32-C6
controller node -> Thread/Matter mesh -> ESP32-C6 LED nodes. USB serial is the
first operator ingress, and controller-local Wi-Fi is allowed for laptop/mobile
convenience. There is no venue Wi-Fi, Ethernet, cloud, or internet requirement.
See `docs/architecture.md` for the role glossary and topology diagram; the role
definitions there (UI/operator ingress vs. Matter controller vs. Thread Border
Router vs. OTA Provider vs. OTA Requestor) apply throughout this document.

Current state: ESP-IDF apps exist under `matter-prototype/` for both the LED
node and the controller node. Both build for ESP32-C6. One LED node has been
flashed; the controller node has been flashed with USB serial plus private Wi-Fi
AP operator ingress and booted to the controller shell. Commissioning and
end-to-end hardware validation remain.

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
- Sends operator intent and, in Phase 6, OTA image bytes to the controller node.
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

### Controller Node

- ESP32-C6 Matter controller/commissioner in the same private fabric — the local
  source of truth for scenes, commissioned node inventory, groups, and (later)
  OTA images.
- Receives operator intent and OTA image bytes over USB serial or the controller
  private AP, then translates them into Matter actions on the fabric.
- Sends unicast commands for per-node provisioning.
- Sends Matter group/multicast commands for "all nodes" scene changes.
- Acts as the local **Matter OTA Provider** for offline installs.
- Runs without venue Wi-Fi or internet. Whether it can be a Thread-only embedded
  controller or needs an explicit OpenThread Border Router role for the
  controller path is the key open hardware risk (see below). Controller-local
  Wi-Fi is only operator ingress; LED nodes remain controlled through Thread.

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
- Deliver new compiled LED modes through firmware OTA in Phase 6. Runtime
  effect plugins or scripts are a separate future design.
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
- `SetNodeConfig` is RAM-only in Phase 3. Durable NVS storage is planned for
  Phase 5.
- The controller node registers USB shell helpers: `lo-set-scene`,
  `lo-set-node-config`, and `lo-sync-clock`.
- The controller-node config currently enables private AP ingress and disables
  WiFi station mode by default. **Open hardware risk:** first hardware
  validation must confirm whether ESP-Matter supports a Thread-side embedded
  controller on ESP32-C6, or whether the controller path needs an explicit
  OpenThread Border Router role. Separately, station mode can be enabled as a
  deliberate build-time option for a private/local network without making LED
  nodes Wi-Fi devices or introducing an internet requirement.

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

Prototype OTA target (Phase 6), fully offline:

```text
operator (laptop/mobile)
  -> USB serial or controller-local Wi-Fi: signed + encrypted firmware image
  -> controller node: stores image, acts as Matter OTA Provider
  -> Matter OTA over Thread
  -> LED nodes: Matter OTA Requestors verify + decrypt + apply
```

- The operator loads a signed and encrypted firmware image over USB serial or
  controller-local Wi-Fi. The laptop/phone is only ingress and never joins the
  Matter fabric.
- The controller node stores that image and serves it to commissioned LED nodes
  as the local **Matter OTA Provider** over the offline Matter/Thread fabric. No
  image is fetched from the internet.
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
