# Matter/Thread Prototype Plan

This document captures the new ESP32-C6-only offline mesh direction. The
existing Rust WiFi/UDP firmware remains the known-good fallback while this path
is proven separately.

Current state: initial ESP-IDF app skeletons exist under `matter-prototype/`
for both the LED node and the controller node. They still need first `idf.py`
build validation and hardware testing.

## Decision

Use ESP-Matter over Thread for the next prototype:

- Matter provides the application model, commissioning, fabric security,
  controller roles, group control, and OTA flow.
- Thread/OpenThread provides the offline IPv6 mesh over the ESP32-C6 802.15.4
  radio.
- The first implementation is a private development fabric, not a production
  Matter certification effort.
- Operator ingress is USB serial into a dedicated controller node.

The prototype should be built with ESP-IDF/ESP-Matter instead of trying to
incrementally graft Matter onto the current `no_std` Rust firmware.

## Prototype Roles

### LED Node

- ESP32-C6 Matter-over-Thread device.
- Owns one physical WS2812B segment.
- Implements a vendor custom LED Orchestra cluster.
- Renders the last valid scene locally when the controller is unreachable.
- Enables Matter OTA Requestor in the OTA phase.

### Controller Node

- ESP32-C6 Matter controller/commissioner in the same private fabric.
- Stores local show state and commissioned node inventory.
- Accepts operator commands and OTA image loading over USB serial first.
- Sends unicast commands for per-node provisioning.
- Sends Matter group/multicast commands for "all nodes" scene changes.
- Acts as the local OTA provider/storage path for offline installs.

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

Prototype implementation notes:

- Cluster id is currently `0xFFF1FC00`, using a development vendor id.
- The LED node renders `off`, `solid`, and `rainbow` using ESP-IDF
  `led_strip`.
- `SetNodeConfig` is RAM-only in Phase 3. Durable NVS storage is planned for
  Phase 5.
- The controller node registers USB shell helpers: `lo-set-scene`,
  `lo-set-node-config`, and `lo-sync-clock`.
- The controller-node scaffold disables WiFi station mode; first hardware
  validation must confirm whether the Espressif controller stack works as a
  Thread-only controller or needs an explicit OpenThread border-router setup.

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

Prototype OTA target:

- LED nodes enable Matter OTA Requestor.
- The controller node stores an operator-loaded OTA image from USB serial.
- The controller node serves that image to commissioned LED nodes over the
  local Matter/Thread fabric.
- Use signed and encrypted OTA images for the prototype.
- Reject invalid or wrong-key images and verify rollback/recovery.

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
