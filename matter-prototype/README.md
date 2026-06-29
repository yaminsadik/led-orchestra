# Matter Prototype

This directory is the active firmware lane for LED Orchestra. It contains the
C++ ESP-IDF/ESP-Matter apps for the Thread LED nodes and the separate ESP32-C6
controller, plus the validation material that selected the current
controller/border-router topology.

If you are trying to build or flash current firmware, start with:

- [`led-node/`](led-node/) for the ESP32-C6 strip renderer.
- [`controller-node/`](controller-node/) for the ESP32-C6 Matter
  controller/commissioner.
- [`s3-h2-hub-validation/`](s3-h2-hub-validation/) for the retained S3+H2
  BR-only runbooks.

The completed Rust Wi-Fi/UDP Phase 1/2 implementation is archived on the
`archive/rust-phase-2` branch and is not part of the active path on this branch.

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

## Current Status

- LED-node and controller-node apps build for `esp32c6`.
- The selected topology is the split system: S3+H2 board as BR-only plus a
  separate ESP32-C6 controller.
- Real Matter group control, durable node config, and scheduled scene support are
  in the firmware.
- Two-node Phase 6 hardware proof passed on 2026-06-26: durable config survived
  reset and synchronized scheduled group activation landed within about 10 ms.
- **Offline OTA (Phase 7) is implemented**: LED-node requestor + app rollback +
  Thread-attach health gate, and an offline provider fork (`controller-node/
  components/lo_ota_provider/`) with DCL/TLS removed — `lo-ota-set-image` registers
  a local candidate and the image streams over plain HTTP from a control-LAN host
  ([`s3-h2-hub-validation/lo-ota-image-server.py`](s3-h2-hub-validation/lo-ota-image-server.py)).
  Provider-on controller builds and the provider cluster is verified on hardware
  as endpoint `1`. QueryImage and `BDX:ReceiveInit` dispatch are now proven; the
  current blocker is the image-source network path (`Host is unreachable` when the
  provider-on bench build tries to fetch a laptop `192.168.x.x` HTTP URL with
  operator Wi-Fi disabled). **Remaining:** make the image URL reachable, prove a
  commissioned node downloads + applies over Thread, then prove bad-image rollback
  before installing hard-to-reach LED nodes. The provider-on controller can't
  BLE-commission (single-role BLE); commission with the commissioner-only build,
  then add the provider — OTA rides Thread. See
  [`s3-h2-hub-validation/phase-7-offline-ota.md`](s3-h2-hub-validation/phase-7-offline-ota.md).

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
- WS2812 renderer on GPIO2 for `off`, `solid`, `rainbow`, `fibonacci`,
  `aurora-breathe`, `comet`, `theater-chase`, `palette-cycle`, and `twinkle`.
- Vendor custom cluster `0xFFF1FC00` with `SetScene`, `SetNodeConfig`, and
  `SyncClock` command callbacks.
- Controller-node project with ESP-Matter controller/commissioner
  initialization and standalone operator AP ingress.
- USB shell helpers:
  - `lo-set-scene`
  - `lo-set-node-config`
  - `lo-sync-clock`
- Both LED-node and controller-node apps build for ESP32-C6.
- LED-node images have been flashed successfully to hardware.
- Controller-node images have been flashed with USB serial plus private Wi-Fi AP
  ingress and booted to the controller shell.

## Bring-Up Sequence

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
9. Validate durable config reload and synchronized scheduled group activation.
10. Offline OTA: serve the image over plain HTTP from a control-LAN host
    (`lo-ota-image-server.py`) and prove LED-node OTA download/apply + rollback.
    Provider + requestor + rollback scaffolding are implemented; QueryImage and
    BDX dispatch work, and the next proof needs a controller-reachable image URL
    (see [`s3-h2-hub-validation/phase-7-offline-ota.md`](s3-h2-hub-validation/phase-7-offline-ota.md)).

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
