# Matter Prototype

This directory is the active C++ ESP-IDF/ESP-Matter prototype lane for LED
Orchestra. The completed Rust WiFi/UDP Phase 1/2 implementation is archived on
the `archive/rust-phase-2` branch.

## Target

- Hardware: ESP32-C6 only.
- Network: Matter over Thread using the ESP32-C6 802.15.4 radio for LED-node
  control. No venue Wi-Fi, cloud, or internet is required.
- Fabric: private development Matter fabric.
- Matter controller/commissioner: the dedicated ESP32-C6 controller node (the
  local source of truth for scenes, node inventory, and groups).
- UI / operator ingress: USB serial plus controller-local Wi-Fi for
  laptop/mobile convenience. The controller defaults to a private AP whose
  SSID/password are set in the gitignored `sdkconfig.defaults.local`; station
  mode is an explicit build-time option for private/local networks.
  Laptop/mobile clients are **not** the Matter controller and hold no fabric
  credentials.
- Renderer: ESP-IDF `led_strip` now; FastLED after a focused ESP32-C6 +
  ESP-Matter integration spike.
- OTA (Phase 6): operator loads a signed/encrypted image over USB serial or
  controller-local Wi-Fi; the controller node is the local Matter OTA Provider;
  LED nodes are Matter OTA Requestors. Matter fabric credentials and image
  signing/encryption are separate security layers.

## Layout

| Path | Purpose |
| --- | --- |
| `led-node/` | ESP-IDF app for the Matter-over-Thread LED node. |
| `controller-node/` | ESP-IDF app for the dedicated Matter controller node. |
| `common/` | Shared prototype constants for cluster ids, command ids, tags, and effect ids. |
| `cluster/` | Human-readable LED Orchestra custom cluster contract. |

## Implemented Prototype Slice

- LED node project skeleton with ESP-Matter component-manager dependency.
- ESP32-C6 Thread-oriented `sdkconfig.defaults`.
- WS2812 renderer on GPIO2 for `off`, `solid`, and `rainbow`.
- Vendor custom cluster `0xFFF1FC00` with `SetScene`, `SetNodeConfig`, and
  `SyncClock` command callbacks.
- Controller-node project skeleton with ESP-Matter controller/commissioner
  initialization.
- USB shell helpers:
  - `lo-set-scene`
  - `lo-set-node-config`
  - `lo-sync-clock`
- Both LED-node and controller-node apps build for ESP32-C6.
- One LED-node image has been flashed successfully to hardware.
- One controller-node image has been flashed with USB serial plus private Wi-Fi
  AP ingress and booted to the controller shell.

## Prototype Sequence

1. Install/export ESP-IDF and ESP-Matter tooling from
   `../docs/requirements.md`.
2. Build `led-node/` for `esp32c6`.
3. Build `controller-node/` for `esp32c6`.
4. Flash one LED node and one controller node.
5. Form/join the local Thread network from the controller node.
6. Commission the LED node into the private fabric.
7. Run `lo-set-scene`; confirm physical LED response.
8. Add a second LED node and test group scene commands.
9. Validate FastLED inside the LED-node app and promote it to the rendering
   layer if it works cleanly with ESP-Matter on ESP32-C6.
10. Add controller-node OTA image storage and LED-node OTA requestor support.

The remaining Phase 3 hardware risk is whether ESP-Matter supports a Thread-side
embedded controller on ESP32-C6, or whether the controller path needs an
explicit OpenThread Border Router role. Either outcome keeps LED nodes
controlled through Thread by the controller node. Controller-local Wi-Fi is an
operator ingress choice, not LED-node transport or an internet dependency.

## Build Commands

```bash
. "$IDF_PATH/export.sh"
cd matter-prototype/led-node
idf.py set-target esp32c6
idf.py build

cd ../controller-node
idf.py set-target esp32c6
idf.py build
```

After flashing, the controller shell should expose the usual Espressif
controller commands plus the LED Orchestra helpers. Example custom-cluster
invoke after commissioning node `1` at endpoint `1`:

```text
lo-set-scene 1 1 1 ff0000 128 80
lo-set-scene 1 1 2 000000 128 40
lo-sync-clock 1 1
```

New LED modes are expected to ship as compiled firmware updates through Matter
OTA in Phase 6. Runtime-loadable scripts/plugins are outside the current phase
plan.
