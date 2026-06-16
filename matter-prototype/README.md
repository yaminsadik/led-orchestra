# Matter Prototype

This directory is the active C++ ESP-IDF/ESP-Matter prototype lane for LED
Orchestra. The completed Rust WiFi/UDP Phase 1/2 implementation is archived on
the `archive/rust-phase-2` branch.

## Target

- Hardware: ESP32-C6 LED nodes; the selected controller/BR topology is a
  **separate ESP32-C6 controller** plus the Espressif ESP Thread BR board used
  as **BR-only** (ESP32-S3 host + ESP32-H2 RCP). All-Espressif, not all-C6.
- Network: Matter over Thread for LED-node control (LED nodes on their native C6
  802.15.4 radio; the hub drives the H2 RCP). No venue Wi-Fi, cloud, or internet
  is required to render scenes.
- Fabric: private development Matter fabric.
- Matter controller/commissioner: the controller node — the local source of truth
  for scenes, node inventory, and groups. It requires a real OpenThread Border
  Router and, after the failed one-board validation, remains a **separate C6
  controller** alongside the **S3+H2 BR-only board**. See
  [`../docs/controller-topology-adr.md`](../docs/controller-topology-adr.md).
- Control plane: a Kubernetes cluster authors/validates/schedules program bundles
  and pushes them to the hub over IP; it never reaches LED nodes directly.
- UI / operator ingress: USB serial plus controller-local Wi-Fi for
  laptop/mobile convenience. The controller defaults to a private AP whose
  SSID/password are set in the gitignored `sdkconfig.defaults.local`; station
  mode is an explicit build-time option for private/local networks.
  Laptop/mobile clients are **not** the Matter controller and hold no fabric
  credentials.
- Renderer: ESP-IDF `led_strip` now; FastLED after a focused ESP32-C6 +
  ESP-Matter integration spike.
- OTA (later phase): operator or Kubernetes loads a signed/encrypted image; the
  hub is the local Matter OTA Provider; LED nodes are Matter OTA Requestors.
  Matter fabric credentials and image signing/encryption are separate security
  layers.

## Layout

| Path | Purpose |
| --- | --- |
| `led-node/` | ESP-IDF app for the Matter-over-Thread LED node. |
| `controller-node/` | ESP-IDF app for the dedicated Matter controller node in the selected split topology. |
| `common/` | Shared prototype constants for cluster ids, command ids, tags, and effect ids. |
| `cluster/` | Human-readable LED Orchestra custom cluster contract. |
| `s3-h2-hub-validation/` | Historical one-board validation runbooks + retained BR-only split-topology support. |
| `stage0-br-validation/` | Stage 0 all-C6 BR runbook + evidence (Fallback-1). |

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
5. Bring up the border router on the **selected split topology**:
   **S3+H2 board as BR-only** plus the separate `controller-node` C6. See
   [`s3-h2-hub-validation/`](s3-h2-hub-validation/) for the retained BR-only
   runbooks and the historical one-board validation. The older all-C6 split
   remains the deeper fallback and the historical Stage 0 proof:
   [`stage0-br-validation/README.md`](stage0-br-validation/README.md).
6. Commission the LED node into the private fabric through the border router.
7. Run `lo-set-scene` (or `matter esp controller invoke-cmd <node> 1 0xFFF1FC00 0
   <SetScene-json>`); confirm physical LED response.
8. Add a second LED node and test group scene commands.
9. Validate FastLED inside the LED-node app and promote it to the rendering
   layer if it works cleanly with ESP-Matter on ESP32-C6.
10. Add hub OTA image storage and LED-node OTA requestor support.

The border-router question is resolved: the controller path requires a real
OpenThread Border Router (confirmed on hardware), and the selected architecture
is the **split topology** after the one-board S3+H2 hub failed offline. See
[`../docs/controller-topology-validation.md`](../docs/controller-topology-validation.md).
LED nodes stay controlled through Thread; hub Wi-Fi is ingress/backbone only, not
LED-node transport or an internet dependency.

## Build Commands

Before building the controller-node AP image, put local operator AP credentials
in `controller-node/sdkconfig.defaults.local`. That file is gitignored; start
from `controller-node/sdkconfig.defaults.local.example` and never commit real
SSID/password values.

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
OTA in the OTA phase. Runtime-loadable scripts/plugins are outside the current
phase plan.
